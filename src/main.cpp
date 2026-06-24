#include "ofMain.h"
#include "ofApp.h"
#include "ofAppGLFWWindow.h"
#include <GLFW/glfw3.h>
#include <ApplicationServices/ApplicationServices.h>

// Window sizes when not fullscreen
const int MAIN_WINDOW_X = 512;
const int MAIN_WINDOW_Y = 256;
const int MAIN_WINDOW_WIDTH = 980;
const int MAIN_WINDOW_HEIGHT = 600;
const int GUI_WINDOW_X = 0;
const int GUI_WINDOW_Y = 0;
const int GUI_WINDOW_WIDTH = 1200;
const int GUI_WINDOW_HEIGHT = 1600;

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
  const bool forceSingleScreen = isOptionKeyHeldAtLaunch();
  if (forceSingleScreen) {
    ofLogNotice() << "Option held at launch: forcing single-screen mode";
  }

  // Hold Control at launch to swap which display gets the main visuals vs the
  // GUI, for when macOS has elected the projector (rather than the laptop) as
  // the primary monitor. Only meaningful with multiple monitors.
  const bool swapDisplays = isControlKeyHeldAtLaunch();
  if (swapDisplays) {
    ofLogNotice() << "Control held at launch: swapping main/gui displays";
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
    mainSettings = createWindowSettings({ static_cast<float>(MAIN_WINDOW_X), static_cast<float>(MAIN_WINDOW_Y) },
                                        { static_cast<float>(MAIN_WINDOW_WIDTH), static_cast<float>(MAIN_WINDOW_HEIGHT) },
                                        false);
    guiSettings = createWindowSettings({ static_cast<float>(GUI_WINDOW_X), static_cast<float>(GUI_WINDOW_Y) },
                                        { static_cast<float>(GUI_WINDOW_WIDTH), static_cast<float>(GUI_WINDOW_HEIGHT) },
                                        false);
  }
  
  auto mainWindow = ofCreateWindow(mainSettings);
  guiSettings.shareContextWith = mainWindow;
  auto guiWindow = ofCreateWindow(guiSettings);

  auto mainApp = std::make_shared<ofApp>();
  mainApp->setGuiWindowPtr(guiWindow);
  mainApp->attachGuiWindowListeners();
	ofRunApp(mainWindow, mainApp);

	ofRunMainLoop();
}
