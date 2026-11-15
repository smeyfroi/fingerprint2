#include <csignal>
#include <cstdlib>

#include "ofMain.h"
#include "ofApp.h"
#include "ofAppGLFWWindow.h"
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#include <ApplicationServices/ApplicationServices.h>

// Single-screen (windowed) layout: the main visuals take the right edge of the
// screen at this fraction of its width; the GUI gets the remainder on the left.
const float MAIN_WINDOW_WIDTH_FRACTION = 0.3f;

namespace {

ofGLFWWindowSettings createWindowSettings(glm::vec2 position, glm::vec2 size, bool fullscreen) {
  ofGLFWWindowSettings settings;
  settings.setGLVersion(4, 1);
  settings.setPosition(position);
  settings.setSize(static_cast<int>(size.x), static_cast<int>(size.y));
  settings.title = "MarkSynth";

  if (fullscreen) {
    settings.decorated = false;
    settings.resizable = false;
  } else {
    settings.decorated = true;
    settings.resizable = true;
  }
  
  return settings;
}

// Returns true if either Option/Alt key is physically held right now. We read
// the modifier straight from the OS because main() runs before any oF window or
// event loop exists, so ofGetKeyPressed() is not usable here.
bool isOptionKeyHeldAtLaunch() {
  return (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState)
          & kCGEventFlagMaskAlternate) != 0;
}

// The laptop's own screen, where the GUI belongs (owner 2026-10-31: "the main should always be
// my laptop and the extended display is the audience display"), or -1 on a Mac with none. Found
// by hardware: which screen macOS makes primary (GLFW monitor 0) follows the menu bar and can
// change between gigs.
int builtInMonitorIndex(GLFWmonitor** monitors, int count) {
  for (int i = 0; i < count; ++i) {
    if (CGDisplayIsBuiltin(glfwGetCocoaMonitor(monitors[i]))) return i;
  }
  return -1;
}

// Returns true if either Control key is physically held right now. Read the same
// OS-level way as the Option check above. Used as a launch-time toggle to swap
// which physical display shows the main visuals vs the GUI, for a set-up where
// the visuals belong on the laptop's screen.
bool isControlKeyHeldAtLaunch() {
  return (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState)
          & kCGEventFlagMaskControl) != 0;
}

// Returns true if either Shift key is physically held right now (OS-level, like
// the checks above). Held at launch to force the session-config chooser dialog
// instead of reusing the remembered config. Read here because ofGetKeyPressed()
// can't see the key in ofApp::setup(), where the chooser runs — which is why the
// chooser's own forceChooseKey check never fired.
bool isShiftKeyHeldAtLaunch() {
  return (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState)
          & kCGEventFlagMaskShift) != 0;
}

} // namespace

//========================================================================
int main(int argc, char** argv) {
  // A send() on a socket the OS has torn down raises SIGPIPE, whose default
  // action kills the app mid-show. macOS does this to the OSC echo socket the
  // moment Local Network privacy refuses it (NECP marks it defunct), and can
  // on a network change. Ignored process-wide, the send fails with EPIPE
  // instead and only the echo to the surface is lost. Installed here, before
  // any socket exists, rather than relying on the video recorder's own
  // SIG_IGN, which only runs once a recording starts.
  std::signal(SIGPIPE, SIG_IGN);

  std::filesystem::path initialSession;
  bool initialStudio = false;
  // --practice (play-live-practice.sh): the live session for a rehearsal, with its recording on the
  // first wake and its auto snapshots off whatever the session says.
  bool practice = false;
  // --no-osc: the iPad's port is not bound and nothing is echoed. For a launch that runs by
  // itself beside the rig (a take replayed, a fingerprint captured): it cannot take the port the
  // live app needs, and a touch on the iPad cannot reach it.
  bool osc = true;
  // --no-midi: the MIDI controllers are left alone: no port is opened and no lamp is set. For a
  // launch that runs by itself beside the rig, as --no-osc is: the copy that captures a
  // fingerprint must not take the APC, the nanoKONTROL2 or the Launch Control from the app
  // that the performer plays.
  bool midi = true;
  // --osc-ports IN OUT: OSC on other ports than the iPad's 8000 and 9000, for a launch that a
  // test client drives while the surface keeps its own.
  int oscReceivePort = 0;
  int oscSendPort = 0;
  // --one-window: on one screen, the Controls window takes the whole screen and the Output
  // window stays behind it. The Perform tab then has its full size, and shows the output itself.
  bool oneWindow = false;
  // --tab NAME: the tab of the Controls window that the launch opens on, by its label
  // ("patch", "live-calibration"). Without it: Perform, or Studio for a studio launch.
  std::string initialTab;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--session" && i + 1 < argc) initialSession = argv[++i];
    else if (argument == "--studio") initialStudio = true;
    else if (argument == "--practice") practice = true;
    else if (argument == "--no-osc") osc = false;
    else if (argument == "--no-midi") midi = false;
    else if (argument == "--one-window") oneWindow = true;
    else if (argument == "--tab" && i + 1 < argc) initialTab = argv[++i];
    else if (argument == "--osc-ports" && i + 2 < argc) {
      oscReceivePort = std::atoi(argv[++i]);
      oscSendPort = std::atoi(argv[++i]);
    }
  }
  // init GLFW manually (since no OF window yet)
  if(!glfwInit()){
      ofLogError() << "Could not init GLFW";
      return -1;
  }
  
  int count;
  GLFWmonitor** monitors = glfwGetMonitors(&count);
  vector<glm::vec2> monitorSizes(count);
  vector<glm::vec2> monitorPositions(count);
  for(int i = 0; i < count; i++){
    const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
    int x, y;
    glfwGetMonitorPos(monitors[i], &x, &y);
    ofLogNotice() << "Monitor " << i
                  << ": " << mode->width << "x" << mode->height
                  << " at position " << x << "," << y
                  << (CGDisplayIsBuiltin(glfwGetCocoaMonitor(monitors[i])) ? " (built-in)" : "");
    monitorSizes[i] = {(float)mode->width, (float)mode->height};
    monitorPositions[i] = {(float)x, (float)y};
  }
  
  // A studio launch (play-recorded.sh, --studio) tunes beside the editor on one screen; a live
  // launch (play-live*.sh) takes both screens when there are two. Hold Option/Alt at launch to
  // force the single-screen windowed layout anyway (parallels Shift-to-rechoose-config).
  const bool optionHeld = isOptionKeyHeldAtLaunch();
  const bool forceSingleScreen = initialStudio || optionHeld;
  if (forceSingleScreen) {
    ofLogNotice() << (optionHeld ? "Option held at launch: forcing single-screen mode"
                                 : "Studio launch: single-screen mode");
  }

  // Hold Control at launch to swap which display gets the main visuals vs the
  // GUI. Only meaningful with multiple monitors.
  const bool swapDisplays = isControlKeyHeldAtLaunch();
  if (swapDisplays) {
    ofLogNotice() << "Control held at launch: swapping main/gui displays";
  }

  // Hold Shift at launch to re-choose the session config (instead of booting the
  // remembered one). Detected OS-level here for the reasons above; passed to ofApp.
  const bool forceChooseConfig = isShiftKeyHeldAtLaunch();
  if (forceChooseConfig) {
    ofLogNotice() << "Shift held at launch: choosing session config";
  }

  // The GUI on the laptop's own screen and the visuals on the other, the audience's. A Mac with no
  // built-in screen among them puts the GUI on the primary, monitor 0.
  const int builtInMonitorId = builtInMonitorIndex(monitors, count);
  int guiMonitorId = builtInMonitorId >= 0 ? builtInMonitorId : 0;
  int mainMonitorId = count > 1 ? (guiMonitorId == 0 ? 1 : 0) : 0;

  if (swapDisplays && count > 1) {
    std::swap(mainMonitorId, guiMonitorId);
  }

  ofGLFWWindowSettings mainSettings, guiSettings;
  if (!forceSingleScreen && count > 1) {
    mainSettings = createWindowSettings(monitorPositions[mainMonitorId],
                                        monitorSizes[mainMonitorId],
                                        true);
    guiSettings = createWindowSettings(monitorPositions[guiMonitorId],
                                       monitorSizes[guiMonitorId],
                                       true);
  } else {
    // Single-screen windowed layout for tuning: GUI on the left 0.7 of the
    // screen, main visuals on the right 0.3 flush to the screen's right edge.
    // MAIN keeps a normal 16:9 aspect (the gig projector / recording shape)
    // rather than stretching to the full screen height. It sits on the GUI's screen, the laptop's.
    const glm::vec2 screenPos = count > 0 ? monitorPositions[guiMonitorId] : glm::vec2 { 0.0f, 0.0f };
    const glm::vec2 screenSize = count > 0 ? monitorSizes[guiMonitorId] : glm::vec2 { 1440.0f, 900.0f };
    const float mainWidth = screenSize.x * MAIN_WINDOW_WIDTH_FRACTION;
    const float mainHeight = mainWidth * 9.0f / 16.0f;
    const float guiWidth = oneWindow ? screenSize.x : screenSize.x - mainWidth;
    // With --one-window the Output window keeps its place and size, behind the Controls window.
    mainSettings = createWindowSettings({ screenPos.x + screenSize.x - mainWidth, screenPos.y },
                                        { mainWidth, mainHeight },
                                        false);
    guiSettings = createWindowSettings({ screenPos.x, screenPos.y },
                                       { guiWidth, screenSize.y },
                                       false);
  }
  
  mainSettings.title = "MarkSynth — Output";
  guiSettings.title = "MarkSynth — Controls";
  if (practice) {
    mainSettings.title += " (practice)";
    guiSettings.title += " (practice)";
  }
  auto mainWindow = ofCreateWindow(mainSettings);
  guiSettings.shareContextWith = mainWindow;
  auto guiWindow = ofCreateWindow(guiSettings);

  auto mainApp = std::make_shared<ofApp>();
  mainApp->setForceChooseConfig(forceChooseConfig);
  mainApp->setInitialSession(initialSession);
  mainApp->setInitialStudio(initialStudio);
  mainApp->setInitialTab(initialTab);
  mainApp->setPractice(practice);
  mainApp->setOscEnabled(osc);
  mainApp->setMidiEnabled(midi);
  if (oscReceivePort > 0 && oscSendPort > 0) mainApp->setOscPorts(oscReceivePort, oscSendPort);
  mainApp->setGuiWindowPtr(guiWindow);
  mainApp->attachGuiWindowListeners();
  // Cmd-Q quits, from the app's menu. Esc does not (owner 2026-11-14): it is the key that
  // leaves a text field or a popup of the GUI, and openFrameworks would end the show on it.
  ofSetEscapeQuitsApp(false);
	ofRunApp(mainWindow, mainApp);

	ofRunMainLoop();
}
