#!/usr/bin/env bash
#
# refresh-xcode-project.sh — put new (or removed) engine/app source files into the
# Xcode project, without losing what projectGenerator overwrites.
#
# WHEN: only when source FILES appear or disappear (a new Mod in ofxMarkSynth, a new
# file in src/). The Makefile build globs its sources and needs nothing; Xcode lists
# every file, so it needs this.
#
# WHAT projectGenerator gets wrong here, all of it learned the hard way:
#   * Its option values must NOT be passed with a space. `-o <path>` does not consume
#     <path>: the path becomes the project to generate, so `-o $OF_ROOT ... ` writes a
#     whole stray project INTO the openFrameworks root (Makefile, config.make,
#     addons.make, src/, bin/, <name>.xcodeproj ...). `-p osx .` likewise leaves a
#     stray `osx/` folder. This script passes the OF path in PG_OF_PATH and nothing else.
#   * It rewrites this project's Makefile, throwing away the wrapper that bundles
#     Syphon.framework into the make-built app. Without it `make` produces an app that
#     dies at launch: "Library not loaded: @loader_path/../Frameworks/Syphon.framework".
#   * It overwrites Project.xcconfig, which holds the product name, bundle id and
#     version (see docs/Release-Build.md).
# Both files are restored from git below, so commit them before running this.
#
# Usage: ./refresh-xcode-project.sh [--build]
#   --build also runs the Debug scheme through xcodebuild and checks the bundle. That
#   leaves the Release app in bin/ alone.
#
set -euo pipefail
cd "$(dirname "$0")"

OF_ROOT=${OF_ROOT:-$(cd ../../.. && pwd)}
PG="$OF_ROOT/projectGenerator/projectGenerator.app/Contents/Resources/app/app/projectGenerator"
PROJECT=fingerprint2.xcodeproj/project.pbxproj
RESTORE=(Makefile Project.xcconfig)

[ -x "$PG" ] || { echo "no projectGenerator at $PG"; exit 1; }

# The files this restores from git must have nothing uncommitted, or it would throw it away.
dirty=$(git status --porcelain -- "${RESTORE[@]}" | awk '{print $2}')
[ -z "$dirty" ] || { echo "commit or revert these first (this script restores them): $dirty"; exit 1; }

# Every run rewrites the project's internal identifiers, so a regeneration that changed
# no file still shows as thousands of changed lines. Compare the file names it lists, and
# put the project back if they are the same.
names_before=$(grep -o '"name": "[^"]*"' "$PROJECT" | sort | uniq -c)

echo "== regenerating (OF at $OF_ROOT)"
PG_OF_PATH="$OF_ROOT" "$PG" . | tail -2

echo "== putting back what it overwrites"
git checkout -- "${RESTORE[@]}"

if [ "$names_before" = "$(grep -o '"name": "[^"]*"' "$PROJECT" | sort | uniq -c)" ]; then
  git checkout -- "$PROJECT"
  echo "no source file was added or removed; the project is left as it was"
  exit 0
fi

echo "== checks"
grep -q "bundle_syphon" Makefile \
  || { echo "FAIL: the Makefile lost its Syphon wrapper"; exit 1; }
grep -q "PBXCopyFilesBuildPhase" "$PROJECT" \
  || { echo "FAIL: the project lost its copy-files phase (Syphon is embedded by it)"; exit 1; }
grep -q "Syphon.framework" "$PROJECT" \
  || { echo "FAIL: the project no longer names Syphon.framework"; exit 1; }
git diff --stat -- "$PROJECT" | tail -1
git status --porcelain | grep -v "xcuserdata" || true

if [ "${1:-}" = "--build" ]; then
  echo "== xcodebuild, Debug scheme"
  xcodebuild -project fingerprint2.xcodeproj -scheme "fingerprint2 Debug" \
             -configuration Debug build > /tmp/refresh-xcode-build.log 2>&1 \
    || { echo "FAIL: build failed, see /tmp/refresh-xcode-build.log"; exit 1; }
  grep -c "BUILD SUCCEEDED" /tmp/refresh-xcode-build.log > /dev/null && echo "BUILD SUCCEEDED"
  ls bin/*Debug.app/Contents/Frameworks/Syphon.framework > /dev/null 2>&1 \
    || ls bin/*_debug.app/Contents/Frameworks/Syphon.framework > /dev/null 2>&1 \
    || { echo "FAIL: the built bundle has no Syphon.framework"; exit 1; }
  echo "the built bundle carries Syphon.framework"
fi

cat <<'NEXT'

Next:
  1. Check the diff is only file references (git diff fingerprint2.xcodeproj/project.pbxproj).
  2. Build it if you have not: ./refresh-xcode-project.sh --build, or
     xcodebuild -project fingerprint2.xcodeproj -scheme "fingerprint2 Debug" -configuration Debug build
  3. Commit the project file ALONE; the xcuserdata state is Xcode's own window state:
     git commit --only fingerprint2.xcodeproj/project.pbxproj -m "Refresh Xcode references: ..."
NEXT
