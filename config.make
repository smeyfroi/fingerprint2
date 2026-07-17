# fingerprint2 — project-local makefile config.
#
# ofxMarkSynth uses std::jthread / std::stop_token (rendering/), which libc++
# gates behind -mmacosx-version-min >= 11.0. The Xcode build already targets 11.5
# (Project.xcconfig: MACOSX_DEPLOYMENT_TARGET = 11.5); pin the makefile build
# (`make Release`) to match, otherwise it inherits the OF default 10.15 and the
# addon's jthread code fails to compile. Mirrors the tool + tests config
# (addons/ofxMarkSynth/tools/motion-replay/config.make, tests/Makefile).
MAC_OS_MIN_VERSION = 11.5
