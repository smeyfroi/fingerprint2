# fingerprint2 (openFrameworks) – agent instructions
Build (Xcode CLI, the app to play): `xcodebuild -project fingerprint2.xcodeproj -scheme "fingerprint2 Release" -configuration Release build`
Build (Xcode CLI, Debug): `xcodebuild -project fingerprint2.xcodeproj -scheme "fingerprint2 Debug" -configuration Debug build`
Artifacts: `bin/Fingerprints.app` and `bin/FingerprintsDebug.app`. `App.xcconfig` gives them the bundle ids `com.meyfroidt.fingerprint2` (`.debug`), the icon and a Developer ID signature, so the macOS grants (Local Network for the iPad, microphone, camera) stay across rebuilds.
Build (Makefile): `make` (Release), `make Debug`, `make clean`. It does not read `App.xcconfig`: it makes an ad-hoc signed `bin/fingerprint2.app` with the openFrameworks bundle id and icon. Use it only for a compile check, then delete it: every grid's `play-*.sh` opens the newer of `fingerprint2.app` and `Fingerprints.app`.
Lint/Format: none configured (no `.clang-format`, `clang-tidy`, etc.).
Tests: none in repo (no single-test runner); validate via build + manual run.
Dependencies: add/remove addons via `addons.make` and regenerate Xcode if needed.
Xcode project: new/removed source files (e.g. a new Mod in ofxMarkSynth) need `./refresh-xcode-project.sh [--build]`; NEVER run projectGenerator by hand - its options eat their own path and it throws away this Makefile's Syphon bundling. See [fingerprint2-xcode-project.md](../../../addons/ofxMarkSynth/docs/dev/fingerprint2-xcode-project.md).
C++ standard: C++23 (see `Project.xcconfig`).
Indentation: 2 spaces; avoid introducing tabs.
Braces: K&R (`if (...) {`), space before `{`.
Headers: use `#pragma once`; keep config constants near top.
Includes: std/system `<...>` first, then local/oF `"..."`; include own header first in .cpp.
Namespaces: avoid `using namespace` in headers; ok in .cpp (keep scope small).
Naming: `UpperCamelCase` types, `lowerCamelCase` funcs/vars; suffix pointers with `Ptr`; keep existing ALL_CAPS globals/macros.
Types/ownership: prefer `constexpr`; use `std::unique_ptr` for ownership and `std::shared_ptr` when shared.
Optionals/null: check before dereference; prefer early-return guard clauses.
Error handling: prefer `ofLogNotice/ofLogWarning/ofLogError`; avoid throwing across oF callbacks.
Paths: prefer `data/` + `ofToDataPath`; avoid adding new hard-coded absolute user paths.
Editor rules: no `.cursor/rules`, `.cursorrules`, or `.github/copilot-instructions.md` present.
Intent system: see `addons/ofxMarkSynth/docs/intent-implementation.md` for the fluent `IntentMap` API used in `Mod::applyIntent()`.
