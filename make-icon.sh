#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 path/to/source-image.jpg"
  echo
  echo "Creates icon.icns in this project's root directory."
}

if [[ $# -ne 1 ]]; then
  usage >&2
  exit 64
fi

source_image="$1"
if [[ ! -f "$source_image" ]]; then
  echo "Source image not found: $source_image" >&2
  exit 66
fi

if ! command -v sips >/dev/null 2>&1; then
  echo "sips is required, but was not found." >&2
  exit 69
fi

if ! command -v iconutil >/dev/null 2>&1; then
  echo "iconutil is required, but was not found." >&2
  exit 69
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
output_file="$script_dir/icon.icns"
tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/fingerprint2-icon.XXXXXX")"

cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

width="$(sips -g pixelWidth "$source_image" 2>/dev/null | awk '/pixelWidth:/ {print $2}')"
height="$(sips -g pixelHeight "$source_image" 2>/dev/null | awk '/pixelHeight:/ {print $2}')"

if [[ -z "$width" || -z "$height" || ! "$width" =~ ^[0-9]+$ || ! "$height" =~ ^[0-9]+$ ]]; then
  echo "Could not read image dimensions from: $source_image" >&2
  exit 65
fi

if (( width < 16 || height < 16 )); then
  echo "Source image must be at least 16x16 pixels." >&2
  exit 65
fi

side="$width"
if (( height < width )); then
  side="$height"
fi

offset_x=$(((width - side) / 2))
offset_y=$(((height - side) / 2))

square_png="$tmp_dir/source-square.png"
iconset_dir="$tmp_dir/icon.iconset"
mkdir "$iconset_dir"

sips \
  -s format png \
  --cropOffset "$offset_y" "$offset_x" \
  --cropToHeightWidth "$side" "$side" \
  "$source_image" \
  --out "$square_png" >/dev/null

make_icon_png() {
  local size="$1"
  local name="$2"

  sips \
    -s format png \
    --resampleHeightWidth "$size" "$size" \
    "$square_png" \
    --out "$iconset_dir/$name" >/dev/null
}

make_icon_png 16 "icon_16x16.png"
make_icon_png 32 "icon_16x16@2x.png"
make_icon_png 32 "icon_32x32.png"
make_icon_png 64 "icon_32x32@2x.png"
make_icon_png 128 "icon_128x128.png"
make_icon_png 256 "icon_128x128@2x.png"
make_icon_png 256 "icon_256x256.png"
make_icon_png 512 "icon_256x256@2x.png"
make_icon_png 512 "icon_512x512.png"
make_icon_png 1024 "icon_512x512@2x.png"

iconutil -c icns "$iconset_dir" -o "$output_file"

echo "Created $output_file"
