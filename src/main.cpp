#include "ofMain.h"
#include "ofApp.h"
#include "ofAppGLFWWindow.h"
#include <GLFW/glfw3.h>
#include <ApplicationServices/ApplicationServices.h>

// Single-screen (windowed) layout: the main visuals take the right edge of the
// screen at this fraction of its width; the GUI gets the remainder on the left.
const float MAIN_WINDOW_WIDTH_FRACTION = 0.3f;

// Set to 1 to compile in the single-screen windowed layout unconditionally —
// the build-time equivalent of holding Option at launch, for tuning stretches
// with frequent relaunches where MarkSynth shares one screen with the editor.
// Leave 0 for gigs; the Option key still works as a per-launch override.
#define FORCE_SINGLE_SCREEN 1

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

// Returns true if either Control key is physically held right now. Read the same
// OS-level way as the Option check above. Used as a launch-time toggle to swap
// which physical display shows the main visuals vs the GUI: the screen macOS
// treats as primary (GLFW monitor 0) can flip between gigs, which otherwise
// lands the GUI on the projector instead of the laptop.
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
int main() {
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
                  << " at position " << x << "," << y;
    monitorSizes[i] = {(float)mode->width, (float)mode->height};
    monitorPositions[i] = {(float)x, (float)y};
  }
  
  // Hold Option/Alt at launch to force the single-screen windowed layout even
  // when a second monitor is connected (parallels Shift-to-rechoose-config).
  // FORCE_SINGLE_SCREEN short-circuits the key check at build time.
  const bool forceSingleScreen = FORCE_SINGLE_SCREEN || isOptionKeyHeldAtLaunch();
  if (forceSingleScreen) {
    ofLogNotice() << (FORCE_SINGLE_SCREEN
                          ? "FORCE_SINGLE_SCREEN compiled in: single-screen mode"
                          : "Option held at launch: forcing single-screen mode");
  }

  // Hold Control at launch to swap which display gets the main visuals vs the
  // GUI, for when macOS has elected the projector (rather than the laptop) as
  // the primary monitor. Only meaningful with multiple monitors.
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

  int mainMonitorId = 0;
  int guiMonitorId = count > 1 ? 1 : 0;

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
    // rather than stretching to the full screen height.
    const glm::vec2 screenPos = count > 0 ? monitorPositions[0] : glm::vec2 { 0.0f, 0.0f };
    const glm::vec2 screenSize = count > 0 ? monitorSizes[0] : glm::vec2 { 1440.0f, 900.0f };
    const float mainWidth = screenSize.x * MAIN_WINDOW_WIDTH_FRACTION;
    const float mainHeight = mainWidth * 9.0f / 16.0f;
    const float guiWidth = screenSize.x - mainWidth;
    mainSettings = createWindowSettings({ screenPos.x + guiWidth, screenPos.y },
                                        { mainWidth, mainHeight },
                                        false);
    guiSettings = createWindowSettings({ screenPos.x, screenPos.y },
                                       { guiWidth, screenSize.y },
                                       false);
  }
  
  auto mainWindow = ofCreateWindow(mainSettings);
  guiSettings.shareContextWith = mainWindow;
  auto guiWindow = ofCreateWindow(guiSettings);

  auto mainApp = std::make_shared<ofApp>();
  mainApp->setForceChooseConfig(forceChooseConfig);
  mainApp->setGuiWindowPtr(guiWindow);
  mainApp->attachGuiWindowListeners();
	ofRunApp(mainWindow, mainApp);

	ofRunMainLoop();
}
