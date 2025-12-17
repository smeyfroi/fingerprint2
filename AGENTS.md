# fingerprint2 (openFrameworks) – agent instructions
Build (Makefile): `make` (Release), `make Debug`, `make clean`, `make RunRelease`, `make RunDebug`
Build (Xcode CLI): `xcodebuild -project fingerprint2.xcodeproj -scheme "fingerprint2 Debug" -configuration Debug build`
Build (Xcode CLI): `xcodebuild -project fingerprint2.xcodeproj -scheme "fingerprint2 Release" -configuration Release build`
Artifacts: app bundles land in `bin/` (see `Project.xcconfig`).
Lint/Format: none configured (no `.clang-format`, `clang-tidy`, etc.).
Tests: none in repo (no single-test runner); validate via build + manual run.
Dependencies: add/remove addons via `addons.make` and regenerate Xcode if needed.
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
