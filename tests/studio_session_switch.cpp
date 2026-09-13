#include <cstdlib>
#include <iostream>

#include "ofApp.h"
#include "ofMain.h"
#include "ofxImGui.h"

namespace {
void require(bool ok, const char* message) {
  if (!ok) { std::cerr << "FAIL: " << message << std::endl; std::exit(2); }
}
class StudioSessionApp : public ofApp {
public:
  std::vector<std::filesystem::path> sessions;
  void update() override {
    if (frames == 20 && index + 1 < sessions.size()) {
      ++index;
      std::cout << "Switching session " << index << std::endl;
      queueStudioSession(sessions[index], false); // keep the user's remembered session
      frames = 0;
    }
    ofApp::update();
    require(currentSessionPath() == std::optional(sessions[index]), "requested session is active");
    require(ImGui::GetCurrentContext() != nullptr, "live ImGui context after session replacement");
    require(ImGui::GetIO().Fonts->Fonts.size() > 0, "fonts recreated");
    if (++frames == 20 && index + 1 == sessions.size()) {
      std::cout << "PASS: " << sessions.size()-1 << " host session replacements with Studio drawing between them" << std::endl;
      ofExit(0);
    }
  }
private:
  int frames = 0;
  size_t index = 0;
};
}
int main(int argc, char** argv) {
  require(argc == 3, "supply two disposable session.json paths");
  ofGLFWWindowSettings outputSettings;
  outputSettings.setGLVersion(4,1);
  outputSettings.setSize(640,360);
  outputSettings.title = "Studio regression output";
  auto output = ofCreateWindow(outputSettings);
  ofGLFWWindowSettings controlsSettings;
  controlsSettings.setGLVersion(4,1);
  controlsSettings.setSize(1000,800);
  controlsSettings.title = "Studio regression controls";
  controlsSettings.shareContextWith = output;
  auto controls = ofCreateWindow(controlsSettings);
  auto app = std::make_shared<StudioSessionApp>();
  auto a = std::filesystem::absolute(argv[1]), b = std::filesystem::absolute(argv[2]);
  app->sessions = {a,b,a,b,a};
  app->setInitialSession(a);
  app->setInitialStudio(true);
  app->setGuiWindowPtr(controls);
  app->attachGuiWindowListeners();
  ofRunApp(output,app);
  ofRunMainLoop();
}
