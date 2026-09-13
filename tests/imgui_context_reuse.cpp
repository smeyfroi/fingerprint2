#include <cstdlib>
#include <iostream>
#include <memory>

#include "ofMain.h"
#include "ofxImGui.h"

namespace {
void require(bool ok, const char* message) {
  if (!ok) { std::cerr << "FAIL: " << message << std::endl; std::exit(2); }
}
class ContextReuseApp : public ofBaseApp {
public:
  void setup() override {
    auto window = ofGetCurrentWindow();
    ofxImGui::Gui owner;
    require(owner.setup(window, nullptr, false) == ofxImGui::Master, "first owner");
    owner.exit();
    owner.exit(); // explicit shutdown followed by destruction must be harmless
    std::cout << "Recreating the GUI on the same live window" << std::endl;
    require(owner.setup(window, nullptr, false) == ofxImGui::Master, "reuse same instance");
    require(ImGui::GetCurrentContext() != nullptr, "recreated current context");
    ofxImGui::Gui shared;
    require(shared.setup(window, nullptr, false) == ofxImGui::Slave, "shared client");
    require(owner.isInSharedMode(), "shared reference count");
    shared.exit();
    require(!owner.isInSharedMode(), "released shared reference");
    require(shared.setup(window, nullptr, false) == ofxImGui::Slave, "reuse shared instance");
    owner.exit();
    ofxImGui::Gui next;
    require(next.setup(window, nullptr, false) == ofxImGui::Error, "reject a dead shared context");
    shared.exit();
    require(next.setup(window, nullptr, false) == ofxImGui::Master, "new owner after all old clients leave");
    next.exit();
    for (int i = 0; i < 4; ++i) {
      gui = std::make_unique<ofxImGui::Gui>();
      require(gui->setup(window, nullptr, false) == ofxImGui::Master, "fresh owner on reused window");
      gui->exit();
      gui.reset();
    }
    gui = std::make_unique<ofxImGui::Gui>();
    require(gui->setup(window, nullptr, false) == ofxImGui::Master, "final drawing owner");
  }
  void draw() override {
    gui->begin();
    ImGui::Begin("Recreated context");
    ImGui::TextUnformatted("Render after repeated context replacement");
    ImGui::End();
    gui->end();
    gui->draw();
    if (++frames == 5) {
      std::cout << "PASS: repeated GUI ownership, shared-client teardown and rendering" << std::endl;
      ofExit(0);
    }
  }
  void exit() override { if (gui) { gui->exit(); gui.reset(); } }
private:
  std::unique_ptr<ofxImGui::Gui> gui;
  int frames = 0;
};
}
int main() {
  ofGLFWWindowSettings settings;
  settings.setGLVersion(4,1);
  settings.setSize(480,320);
  settings.title = "Studio context regression";
  auto window = ofCreateWindow(settings);
  ofRunApp(window,std::make_shared<ContextReuseApp>());
  ofRunMainLoop();
}
