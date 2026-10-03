#pragma once

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "OscOutboundPacketStream.h"
#include "ofLog.h"
#include "ofxOscMessage.h"

// Sends OSC packets from a thread of its own, so no socket call ever runs on
// the render thread. The main thread serialises each message (a few hundred
// bytes, pure CPU) and queues the bytes; the worker owns the UDP socket,
// resolves the host, and does the send().
//
// Why not ofxOscSender: its send() runs on the caller's thread, puts a 320 KB
// buffer on the stack per call (too close to a secondary thread's 512 KB
// stack to move it there), and keeps its socket private, so SO_NOSIGPIPE
// can't be set. Without that, a send refused by Local Network privacy raises
// SIGPIPE: ignored by main.cpp, but a debugger is told about every one, at
// ~1.3 ms per send.
//
// OVERFLOW POLICY: DROP-OLDEST. The queue holds at most kMaxQueued packets;
// past that the oldest is discarded. Everything sent here is state the
// surface should show NOW (meters, lamps, the grid), so the newest packet is
// the one worth keeping. The queue only fills if the worker is wedged.
//
// Threading: post(), setTarget() and takeLogLines() are main-thread only. The
// worker never calls ofLog (not thread-safe); it queues lines for the main
// thread to log via takeLogLines().
class OscSendThread {
public:
  static constexpr std::size_t kMaxQueued = 512;
  static constexpr std::size_t kScratchBytes = 16384;  // largest packet we build is ~2 KB
  static constexpr int kSendTimeoutMs = 100;  // bounds a blocked send() so stop() can't hang

  OscSendThread() : scratch_(kScratchBytes) {
    worker_ = std::thread([this] { run(); });
  }

  ~OscSendThread() { stop(); }

  OscSendThread(const OscSendThread&) = delete;
  OscSendThread& operator=(const OscSendThread&) = delete;

  // Point the sender at host:port. Resolution and socket setup happen on the
  // worker; packets still queued for the previous host are discarded.
  void setTarget(const std::string& host, int port) {
    {
      std::lock_guard lock(mutex_);
      pendingTarget_ = Target { host, port };
      queue_.clear();
    }
    cv_.notify_one();
  }

  // Serialise on the caller's thread and queue for the worker. Returns false
  // when the message could not be serialised (it is dropped and logged).
  bool post(const ofxOscMessage& m) {
    std::vector<char> packet;
    if (!serialise(m, packet)) return false;
    {
      std::lock_guard lock(mutex_);
      if (queue_.size() >= kMaxQueued) {
        queue_.pop_front();
        ++dropped_;
      }
      queue_.push_back(std::move(packet));
    }
    cv_.notify_one();
    return true;
  }

  // Lines the worker wants logged, plus a summary of any packets dropped.
  std::vector<std::string> takeLogLines() {
    std::lock_guard lock(mutex_);
    std::vector<std::string> lines;
    lines.swap(logLines_);
    if (dropped_ > 0) {
      lines.push_back("Send queue full: dropped " + std::to_string(dropped_) + " oldest packet(s)");
      dropped_ = 0;
    }
    return lines;
  }

  void stop() {
    {
      std::lock_guard lock(mutex_);
      if (stopping_) return;
      stopping_ = true;
    }
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
  }

private:
  struct Target {
    std::string host;
    int port { 0 };
  };

  // Main thread only: scratch_ is shared across calls, not across threads.
  bool serialise(const ofxOscMessage& m, std::vector<char>& out) {
    try {
      osc::OutboundPacketStream p(scratch_.data(), scratch_.size());
      p << osc::BeginMessage(m.getAddress().c_str());
      for (std::size_t i = 0; i < m.getNumArgs(); ++i) {
        switch (m.getArgType(i)) {
          case OFXOSC_TYPE_INT32:  p << m.getArgAsInt32(i); break;
          case OFXOSC_TYPE_FLOAT:  p << m.getArgAsFloat(i); break;
          case OFXOSC_TYPE_STRING: p << m.getArgAsString(i).c_str(); break;
          case OFXOSC_TYPE_TRUE:
          case OFXOSC_TYPE_FALSE:  p << m.getArgAsBool(i); break;
          default:
            ofLogError("OscSendThread") << m.getAddress() << ": unsupported argument type '"
                                        << static_cast<char>(m.getArgType(i)) << "'";
            return false;
        }
      }
      p << osc::EndMessage;
      out.assign(p.Data(), p.Data() + p.Size());
      return true;
    } catch (const osc::OutOfBufferMemoryException&) {
      ofLogError("OscSendThread") << m.getAddress() << ": message larger than "
                                  << kScratchBytes << " bytes, dropped";
      return false;
    }
  }

  void log(std::string line) {
    std::lock_guard lock(mutex_);
    logLines_.push_back(std::move(line));
  }

  // Worker thread only.
  void connectTo(const Target& target) {
    closeSocket();
    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    const std::string port = std::to_string(target.port);
    if (const int rc = getaddrinfo(target.host.c_str(), port.c_str(), &hints, &res); rc != 0 || !res) {
      log("Could not resolve " + target.host + ": " + gai_strerror(rc));
      return;
    }
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
      log(std::string("Could not create socket: ") + std::strerror(errno));
      freeaddrinfo(res);
      return;
    }
    const int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
    const timeval tv { 0, kSendTimeoutMs * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
      log("Could not connect to " + target.host + ": " + std::strerror(errno));
      close(fd);
      freeaddrinfo(res);
      return;
    }
    freeaddrinfo(res);
    fd_ = fd;
    targetName_ = target.host + ":" + port;
    failing_ = false;
  }

  void closeSocket() {
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
  }

  // A refused send fails every time, so report the change, not each failure.
  // ECONNREFUSED is not a failure here: it is the ICMP "port unreachable" from
  // an earlier packet (TouchOSC closed on the iPad), reported on alternate
  // sends, and would make the log flap.
  void sendOne(const std::vector<char>& packet) {
    if (fd_ < 0) return;
    if (send(fd_, packet.data(), packet.size(), 0) >= 0 || errno == ECONNREFUSED) {
      if (failing_) log("Sends to " + targetName_ + " working again");
      failing_ = false;
      return;
    }
    if (!failing_) log("Sends to " + targetName_ + " failing: " + std::strerror(errno));
    failing_ = true;
  }

  void run() {
    pthread_setname_np("OscSendThread");
    std::deque<std::vector<char>> batch;
    for (;;) {
      std::optional<Target> target;
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return stopping_ || pendingTarget_ || !queue_.empty(); });
        if (stopping_) break;
        target.swap(pendingTarget_);
        batch.swap(queue_);
      }
      if (target) connectTo(*target);
      for (const auto& packet : batch) sendOne(packet);
      batch.clear();
    }
    closeSocket();
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::vector<char>> queue_;  // guarded by mutex_
  std::optional<Target> pendingTarget_;  // guarded by mutex_
  std::vector<std::string> logLines_;    // guarded by mutex_
  std::size_t dropped_ { 0 };            // guarded by mutex_
  bool stopping_ { false };              // guarded by mutex_

  std::vector<char> scratch_;  // main thread only

  int fd_ { -1 };              // worker only
  std::string targetName_;     // worker only
  bool failing_ { false };     // worker only

  std::thread worker_;  // last: started after every member above exists
};
