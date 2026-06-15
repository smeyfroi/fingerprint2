#!/usr/bin/env bash
#
# Notarize + staple the Release app for Developer ID distribution.
#
# Signing is owned entirely by App.xcconfig (Developer ID + Hardened Runtime +
# secure timestamp + of.entitlements, scoped to the Release config), so the
# xcodebuild step below already produces a properly signed app. This script only
# adds the steps Xcode can't: the notary-service submission and ticket stapling.
#
# Re-runnable. Bump CURRENT_PROJECT_VERSION in Project.xcconfig for each new submission.
#
set -euo pipefail
cd "$(dirname "$0")"

PROJECT="fingerprint2.xcodeproj"
SCHEME="fingerprint2 Release"
APP="bin/Fingerprints.app"
NOTARY_PROFILE="fingerprints-notary"
ZIP="bin/Fingerprints.zip"

# --- 1. Build Release (signed by App.xcconfig) ----------------------------
echo "==> [1/4] Building Release…"
xcodebuild -project "$PROJECT" -scheme "$SCHEME" -configuration Release build | tail -20
test -d "$APP" || { echo "ERROR: build did not produce $APP"; exit 1; }

# --- 2. Verify the build really is Developer ID signed + hardened ----------
echo "==> [2/4] Verifying signature…"
codesign --verify --deep --strict --verbose=2 "$APP"
codesign -dvv "$APP" 2>&1 | grep -q "Authority=Developer ID Application" \
  || { echo "ERROR: $APP is not Developer ID signed — check App.xcconfig"; exit 1; }
codesign -dvv "$APP" 2>&1 | grep -q "flags=.*runtime" \
  || { echo "ERROR: $APP is missing Hardened Runtime — check App.xcconfig"; exit 1; }

# --- 3. Notarize ----------------------------------------------------------
echo "==> [3/4] Submitting to Apple notary service (may take a few minutes)…"
rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"
xcrun notarytool submit "$ZIP" --keychain-profile "$NOTARY_PROFILE" --wait
rm -f "$ZIP"

# --- 4. Staple + final Gatekeeper assessment ------------------------------
echo "==> [4/4] Stapling ticket and running Gatekeeper assessment…"
xcrun stapler staple "$APP"
xcrun stapler validate "$APP"
spctl -a -t exec -vvv "$APP"

echo
echo "==> DONE: $APP is signed, notarized, and stapled."
