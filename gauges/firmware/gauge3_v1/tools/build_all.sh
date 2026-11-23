#!/bin/sh
# Compile both sketches, copy the app binaries to firmware_bins/, render previews.
# Build output goes to /tmp (scratch). Run from anywhere.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI=arduino-cli
FQBN="esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc"
"$ROOT/tools/check_shared.sh"
for s in hub display; do
  echo "== compile $s"
  $CLI compile --fqbn "$FQBN" --build-path "/tmp/g3b_$s" --warnings all "$ROOT/$s"
  mkdir -p "$ROOT/firmware_bins/$s"
  cp "/tmp/g3b_$s/$s.ino.bin" "/tmp/g3b_$s/$s.ino.bootloader.bin" "/tmp/g3b_$s/$s.ino.partitions.bin" "/tmp/g3b_$s/boot_app0.bin" "$ROOT/firmware_bins/$s/"
done
echo "== previews"
g++ -O2 -std=c++17 -Wall -o /tmp/g3render "$ROOT/tools/render_previews/render.cpp" "$ROOT/display/ui.cpp" "$ROOT/display/gfx.cpp" "$ROOT/display/fonts.cpp"
rm -rf /tmp/g3ppm && mkdir -p /tmp/g3ppm && /tmp/g3render /tmp/g3ppm > /dev/null
python3 "$ROOT/tools/render_previews/make_previews.py" /tmp/g3ppm "$ROOT/previews_v2"   # v2 renders. previews/ = v1 renders, kept
