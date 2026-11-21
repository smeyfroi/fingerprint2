#include "ofApp.h"

#include <exception>
#include <utility>

#include "ofxTimeMeasurements.h"

// Stage 17: the session loader moved out of the ofxMarkSynth umbrella into
// config/session/. Include it directly (previously pulled in via ofxMarkSynth.h).
#include "config/session/SessionResourceLoader.hpp"
#include "config/session/SessionRuntimeSettings.hpp"
#include "config/ResourceKeys.hpp"
#include "subsystem/SynthSubsystems.hpp"

using namespace ofxMarkSynth;

void ofApp::attachGuiWindowListeners() {
  if (!guiWindowPtr || guiWindowListenersAttached) {
    return;
  }

  ofAddListener(guiWindowPtr->events().draw, this, &ofApp::drawGui);
  ofAddListener(guiWindowPtr->events().keyPressed, this, &ofApp::keyPressedEvent);
  ofAddListener(guiWindowPtr->events().keyReleased, this, &ofApp::keyReleasedEvent);
  guiWindowListenersAttached = true;
}

void ofApp::detachGuiWindowListeners() {
  if (!guiWindowPtr || !guiWindowListenersAttached) {
    return;
  }

  ofRemoveListener(guiWindowPtr->events().draw, this, &ofApp::drawGui);
  ofRemoveListener(guiWindowPtr->events().keyPressed, this, &ofApp::keyPressedEvent);
  ofRemoveListener(guiWindowPtr->events().keyReleased, this, &ofApp::keyReleasedEvent);
  guiWindowListenersAttached = false;
}

void ofApp::keyPressedEvent(ofKeyEventArgs& e) {
  if (isShuttingDown || !synthPtr) {
    return;
  }

  keyPressed(e.key);
}

void ofApp::keyReleasedEvent(ofKeyEventArgs& e) {
  if (isShuttingDown || !synthPtr) {
    return;
  }

  keyReleased(e.key);
}

void ofApp::setup(){
  ofDisableArbTex();
  glEnable(GL_PROGRAM_POINT_SIZE);
  ofSetBackgroundColor(0);
  TIME_SAMPLE_SET_DRAW_LOCATION(TIME_MEASUREMENTS_BOTTOM_LEFT);

  if (!initialLaunch.launchFile.empty()) {
    ComposedLaunch launch;
    try {
      launch = composeLaunch(initialLaunch);
    } catch (const std::exception& error) {
      hardExitWithAlert(std::string("Could not compose the launch: ") + error.what());
    }
    currentLaunch = LaunchSelection { launch.launchFile, launch.gridName, initialLaunch.rig };
    applySessionRuntimeSettings(launch.session.json);
    installSynth(buildResourceManagerFromLaunch(launch), initialStudio || launch.kind == "studio");
    if (launch.kind == "performance" && !initialStudio) {   // a Finder launch reopens the performance; a check (--studio) leaves it
      saveLastSessionConfigPath(getSessionConfigPointerFilePath("fingerprint2", "lastSessionConfig.json"), launch.launchFile);
    }
    return;
  }
  if (!initialSession.empty()) {
    const SessionConfig session { std::filesystem::absolute(initialSession), ofLoadJson(initialSession) };
    applySessionRuntimeSettings(session.json);
    installSynth(buildResourceManagerFromSessionConfig(session), initialStudio);
    return;
  }
  auto resources = loadSessionResourceManagerOrExit({
    .appNamespace = "fingerprint2",
    .dialogTitle = "Choose fingerprint2 session config (JSON)",
    .forceChoose = forceChooseConfig,
  });

  installSynth(std::move(resources), initialStudio);
}

void ofApp::installSynth(ResourceManager resources, bool studio) {
  // Practice holds for every session this run opens. A calibration save still writes to the
  // session file, so one done at a rehearsal carries into the show.
  if (practice) {
    resources.add(ResourceKeys::StartRecordingOnFirstWake, false);
    resources.add(ResourceKeys::AutoSnapshotsEnabled, false);
    ofLogNotice("fingerprint2") << "Practice: no recording on the first wake and no auto snapshots";
  }
  // --fingerprints holds for every session this run opens, as --practice does.
  if (!fingerprintsFolder.empty()) {
    resources.add(ResourceKeys::Fingerprints, std::filesystem::absolute(fingerprintsFolder));
    ofLogNotice("fingerprint2") << "Fingerprints from " << std::filesystem::absolute(fingerprintsFolder);
  }
  synthPtr = ofxMarkSynth::Synth::create("fingerprint2", ofxMarkSynth::ModConfig {
  }, resources);
  if (synthPtr->getConfigSubsystem().getCurrentConfigPath().empty()) synthPtr->loadFirstPerformanceConfig();
  if (synthPtr->getConfigSubsystem().getCurrentConfigPath().empty()) {
    throw std::runtime_error("The session has no loadable performance config");
  }
  synthPtr->onStudioSessionRequested = [this](const std::filesystem::path& path) {
    queueStudioSession(path);
  };
  synthPtr->onLaunchGridRequested = [this](const std::string& grid) { pendingLaunchGrid = grid; };
  ofAddListener(synthPtr->configWillUnloadEvent, this, &ofApp::onSynthWillUnload); // before configureGui
  ofAddListener(synthPtr->configDidLoadEvent, this, &ofApp::onSynthDidLoad); // before configureGui
  synthPtr->configureGui(guiWindowPtr);
  if (studio) synthPtr->showStudio();
  if (!initialTab.empty() && !synthPtr->showTab(initialTab)) {
    ofLogWarning("ofApp") << "--tab " << initialTab << ": the Controls window has no tab of that name";
  }
  initialTab.clear();   // once per launch: a reload (another grid, a Studio session) must not replay its steps
}

void ofApp::queueStudioSession(const std::filesystem::path& path, bool remember) {
  pendingStudioSession = StudioSessionRequest { path, remember };
}

std::optional<std::filesystem::path> ofApp::currentSessionPath() const {
  if (!synthPtr) return std::nullopt;
  const auto path = synthPtr->getConfigSubsystem().resources.get<std::filesystem::path>(ResourceKeys::SessionConfigPath);
  return path ? std::optional(*path) : std::nullopt;
}

void ofApp::openStudioSession(const std::filesystem::path& path, bool remember) {
  if (!synthPtr || synthPtr->getRuntimeSubsystem().isRecording()) return;
  const auto previousPath = currentSessionPath();
  std::optional<SessionConfig> previous;
  bool replaced = false;
  try {
    const SessionConfig next { std::filesystem::absolute(path), ofLoadJson(path) };
    auto resources = buildResourceManagerFromSessionConfig(next); // Validate before teardown.
    if (previousPath) previous = SessionConfig { *previousPath, ofLoadJson(*previousPath) };
    ofRemoveListener(synthPtr->configWillUnloadEvent, this, &ofApp::onSynthWillUnload);
    ofRemoveListener(synthPtr->configDidLoadEvent, this, &ofApp::onSynthDidLoad);
    midiController.onSynthWillUnload();
    apcMiniController.onSynthWillUnload();
    nanoKontrolController.onSynthWillUnload();
    oscController.onSynthWillUnload();
    synthPtr->shutdown();
    synthPtr.reset();
    replaced = true;
    applySessionRuntimeSettings(next.json);
    installSynth(std::move(resources), true);
    currentLaunch.reset();   // a session file now plays, not the launch
    if (remember) {
      saveLastSessionConfigPath(getSessionConfigPointerFilePath("fingerprint2", "lastSessionConfig.json"), next.path);
    }
    ofLogNotice("Studio") << "Opened performance " << next.path;
  } catch (const std::exception& error) {
    ofLogError("Studio") << "Could not open session: " << error.what();
    if (replaced && previous) {
      try {
        if (synthPtr) {
          ofRemoveListener(synthPtr->configWillUnloadEvent, this, &ofApp::onSynthWillUnload);
          ofRemoveListener(synthPtr->configDidLoadEvent, this, &ofApp::onSynthDidLoad);
          midiController.onSynthWillUnload();
          apcMiniController.onSynthWillUnload();
          nanoKontrolController.onSynthWillUnload();
          oscController.onSynthWillUnload();
          synthPtr->shutdown();
          synthPtr.reset();
        }
        applySessionRuntimeSettings(previous->json);
        installSynth(buildResourceManagerFromSessionConfig(*previous), true);
      } catch (const std::exception& restoreError) {
        ofLogError("Studio") << "Could not restore previous session: " << restoreError.what();
      }
    }
    ofSystemAlertDialog("Studio could not open that performance. See the log for details.");
  }
}

void ofApp::openLaunchGrid(const std::string& grid) {
  if (!synthPtr || !currentLaunch || synthPtr->getRuntimeSubsystem().isRecording()) return;
  const LaunchSelection previous = *currentLaunch;
  const bool studio = initialStudio;
  auto tearDown = [this] {
    ofRemoveListener(synthPtr->configWillUnloadEvent, this, &ofApp::onSynthWillUnload);
    ofRemoveListener(synthPtr->configDidLoadEvent, this, &ofApp::onSynthDidLoad);
    midiController.onSynthWillUnload();
    apcMiniController.onSynthWillUnload();
    nanoKontrolController.onSynthWillUnload();
    oscController.onSynthWillUnload();
    synthPtr->shutdown();
    synthPtr.reset();
  };
  bool replaced = false;
  try {
    const ComposedLaunch launch = composeLaunch({ previous.launchFile, grid, previous.rig });
    auto resources = buildResourceManagerFromLaunch(launch);   // validate before teardown
    tearDown();
    replaced = true;
    applySessionRuntimeSettings(launch.session.json);
    installSynth(std::move(resources), studio || launch.kind == "studio");
    currentLaunch = LaunchSelection { launch.launchFile, launch.gridName, previous.rig };
    ofLogNotice("fingerprint2") << "Grid " << launch.gridName << " of " << launch.launchFile.string();
  } catch (const std::exception& error) {
    ofLogError("fingerprint2") << "Could not play the grid " << grid << ": " << error.what();
    try {
      if (replaced) {
        if (synthPtr) tearDown();
        const ComposedLaunch back = composeLaunch(previous);
        applySessionRuntimeSettings(back.session.json);
        installSynth(buildResourceManagerFromLaunch(back), studio || back.kind == "studio");
      } else {
        // Nothing was torn down; the binding the failed build set must be the playing launch's again.
        (void)buildResourceManagerFromLaunch(composeLaunch(previous));
      }
    } catch (const std::exception& restoreError) {
      ofLogError("fingerprint2") << "Could not restore the grid " << previous.grid << ": " << restoreError.what();
    }
    ofSystemAlertDialog("Could not play the grid " + grid + ". See the log for details.");
  }
}

void ofApp::onSynthWillUnload(ofxMarkSynth::Synth::ConfigUnloadEvent& e) {
  midiController.onSynthWillUnload();
  apcMiniController.onSynthWillUnload();
  nanoKontrolController.onSynthWillUnload();
  oscController.onSynthWillUnload();
}

void ofApp::onSynthDidLoad(ofxMarkSynth::Synth::ConfigLoadedEvent& e) {
  if (midiEnabled) {
    midiController.onSynthDidLoad(synthPtr);
    apcMiniController.onSynthDidLoad(synthPtr);
    nanoKontrolController.onSynthDidLoad(synthPtr);
  }
  oscController.onSynthDidLoad(synthPtr);
}

//--------------------------------------------------------------
void ofApp::update(){
  if (pendingStudioSession && !isShuttingDown) {
    const auto request = *pendingStudioSession;
    pendingStudioSession.reset();
    openStudioSession(request.path, request.remember);
  }
  if (pendingLaunchGrid && !isShuttingDown) {
    const std::string grid = *pendingLaunchGrid;
    pendingLaunchGrid.reset();
    openLaunchGrid(grid);
  }
  if (!synthPtr || isShuttingDown) {
    return;
  }

  synthPtr->update();
  if (midiEnabled) {
    midiController.update();
    apcMiniController.update();
    nanoKontrolController.update();
  }
  oscController.update();

  // The health lamps of the GUI: which controllers answer.
  synthPtr->setControllerStatus("APC", apcMiniController.isConnected());
  synthPtr->setControllerStatus("NANO", nanoKontrolController.isConnected());
  synthPtr->setControllerStatus("XL3", midiController.isConnected());
  synthPtr->setControllerStatus("IPAD", oscController.hasSurface());
}

//--------------------------------------------------------------
void ofApp::draw(){
  if (!synthPtr || isShuttingDown) {
    return;
  }

  synthPtr->draw();
}

void ofApp::drawGui(ofEventArgs& args){
  if (!synthPtr || isShuttingDown) {
    return;
  }

  synthPtr->drawGui();
}

//--------------------------------------------------------------
void ofApp::exit(){
  if (isShuttingDown) {
    return;
  }

  isShuttingDown = true;
  detachGuiWindowListeners();

  // Clear controller LEDs before tearing down the synth.
  midiController.exit();
  apcMiniController.exit();
  nanoKontrolController.exit();
  oscController.exit();

  if (synthPtr) {
    ofRemoveListener(synthPtr->configWillUnloadEvent, this, &ofApp::onSynthWillUnload);
    ofRemoveListener(synthPtr->configDidLoadEvent, this, &ofApp::onSynthDidLoad);
    synthPtr->shutdown();
    synthPtr.reset();
  }
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
  if (!synthPtr || isShuttingDown) {
    return;
  }

  synthPtr->keyPressed(key);
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){
  if (!synthPtr || isShuttingDown) {
    return;
  }

  synthPtr->keyReleased(key);
}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){
  
}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){
  
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){
  
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){
  
}

//--------------------------------------------------------------
void ofApp::mouseScrolled(int x, int y, float scrollX, float scrollY){
  
}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){
  
}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){
  
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){
  if (!synthPtr || isShuttingDown) {
    return;
  }

  synthPtr->windowResized(w, h);
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){
  
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){
  
}
