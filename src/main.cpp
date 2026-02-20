#include "ofMain.h"
#include "ofApp.h"
#include "ofAppGLFWWindow.h"
#include <GLFW/glfw3.h>

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
  
  int mainMonitorId = 0;
  int guiMonitorId = count > 1 ? 1 : 0;
  
  ofGLFWWindowSettings mainSettings, guiSettings;
  if (count > 1) {
    mainSettings = createWindowSettings(monitorPositions[mainMonitorId], monitorSizes[mainMonitorId], true);
    guiSettings = createWindowSettings(monitorPositions[guiMonitorId], monitorSizes[guiMonitorId], true);
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
  ofAddListener(guiWindow->events().draw, mainApp.get(), &ofApp::drawGui);
  ofAddListener(guiWindow->events().keyPressed, mainApp.get(), &ofApp::keyPressedEvent); // needs adapter because keyPressed doesn't take an ofEventArgs& parameter
  ofAddListener(guiWindow->events().keyReleased, mainApp.get(), &ofApp::keyReleasedEvent); // needs adapter because keyReleased doesn't take an ofEventArgs& parameter
	ofRunApp(mainWindow, mainApp);
	ofRunMainLoop();
}
