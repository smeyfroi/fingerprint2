#!/bin/sh
# Build "Fingerprints Launcher.app" into ../bin from Launcher.applescript.
set -eu
here=$(cd -- "$(dirname -- "$0")" && pwd)
out="$here/../bin/Fingerprints Launcher.app"
rm -rf "$out"
osacompile -o "$out" "$here/Launcher.applescript"
cp "$here/../icon.icns" "$out/Contents/Resources/applet.icns"
echo "built $out"
