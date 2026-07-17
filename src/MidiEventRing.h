#pragma once

#include <array>
#include <atomic>
#include <cstddef>

// Single-producer / single-consumer lock-free ring for handing MIDI events from
// the ofxMidi listener thread to the main (update) thread. Exactly one thread
// may call push() (the MIDI listener) and exactly one may call drain() (main).
//
// One slot is kept permanently empty to disambiguate full from empty without a
// separate count, so the usable capacity is N-1.
//
// OVERFLOW POLICY: DROP-ON-FULL (newest). push() returns false and discards the
// incoming event when the ring is full, rather than advancing the write cursor
// over unread entries. The controllers previously open-coded the ring WITHOUT a
// fullness check: once the writer lapped the reader, writeIndex wrapped to equal
// readIndex, the drain loop then saw the ring as EMPTY and silently dropped a
// whole bufferful, and the producer overwrote the slot the consumer was reading
// (a torn read). Dropping the newest CC in a burst longer than the buffer
// between two update() calls is the safe failure for faders/buttons — the
// already-queued events stay intact and in order. In practice the ring never
// fills under hand-driven MIDI (N-1 events between two 60 fps frames is
// thousands of CC/sec), so this only changes behaviour on a pathological burst.
template <typename T, std::size_t N>
class MidiEventRing {
 public:
  static_assert(N >= 2, "MidiEventRing needs at least one usable slot");

  // Producer side — MIDI listener thread ONLY. Returns false (event dropped) if
  // the ring was full.
  bool push(const T& item) {
    const int w = writeIndex_.load(std::memory_order_relaxed);
    const int next = advance(w);
    if (next == readIndex_.load(std::memory_order_acquire)) {
      return false;  // full — drop the newest event
    }
    buffer_[static_cast<std::size_t>(w)] = item;
    writeIndex_.store(next, std::memory_order_release);
    return true;
  }

  // Consumer side — main thread ONLY. Invokes fn(const T&) for every queued
  // event in FIFO order, then advances the read cursor once. Returns the count
  // drained. fn must not call push()/drain() on this ring.
  template <typename Fn>
  int drain(Fn&& fn) {
    const int w = writeIndex_.load(std::memory_order_acquire);
    int r = readIndex_.load(std::memory_order_relaxed);
    int count = 0;
    while (r != w) {
      fn(static_cast<const T&>(buffer_[static_cast<std::size_t>(r)]));
      r = advance(r);
      ++count;
    }
    readIndex_.store(r, std::memory_order_release);
    return count;
  }

 private:
  static constexpr int advance(int i) { return (i + 1) % static_cast<int>(N); }

  std::array<T, N> buffer_ {};
  std::atomic<int> writeIndex_ { 0 };  // producer writes, consumer reads
  std::atomic<int> readIndex_ { 0 };   // consumer writes, producer reads
};
