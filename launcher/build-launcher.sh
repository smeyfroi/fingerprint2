#!/bin/sh
# Build "Fingerprints Launcher.app" into ../bin from Launcher.applescript, with the app's icon.
# The script takes dropped files, so osacompile makes a droplet: its icon is droplet.icns, and its
# Assets.car and CFBundleIconName win over any .icns. The build removes both and signs the bundle again.
set -eu
here=$(cd -- "$(dirname -- "$0")" && pwd)
out="$here/../bin/Fingerprints Launcher.app"
rm -rf "$out"
osacompile -o "$out" "$here/Launcher.applescript"
resources="$out/Contents/Resources"
for name in droplet applet; do
  [ -f "$resources/$name.icns" ] && cp "$here/../icon.icns" "$resources/$name.icns"
done
rm -f "$resources/Assets.car"
/usr/libexec/PlistBuddy -c "Delete :CFBundleIconName" "$out/Contents/Info.plist" 2>/dev/null || true
codesign --force --sign - "$out"
touch "$out"
echo "built $out"
