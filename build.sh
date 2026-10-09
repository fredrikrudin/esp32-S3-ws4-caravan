#!/bin/sh
# esp32-S3-ws4-caravan v1.0
# Renders the README screenshots with the firmware's own UI code, on a PC (Linux or macOS, g++).
# Usage: sh tools/screenshots/build.sh    (from the sketch folder; output in tools/screenshots/out_*)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SKETCH=$(cd "$HERE/../.." && pwd)
WORK="$HERE/.build"
mkdir -p "$WORK/obj" "$WORK/ui"
# LVGL 8.4, the same version as on the board
[ -d "$WORK/lvgl" ] || git clone -q --depth 1 -b v8.4.0 https://github.com/lvgl/lvgl.git "$WORK/lvgl"
INC="-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -I$HERE/stub -I$WORK -I$SKETCH -I$WORK/lvgl"
if [ ! -f "$WORK/liblvgl.a" ]; then
  for f in $(find "$WORK/lvgl/src" -name '*.c'); do
    gcc -O1 -w -c $INC "$f" -o "$WORK/obj/$(echo "$f" | md5sum | cut -c1-12).o"
  done
  ar rcs "$WORK/liblvgl.a" "$WORK"/obj/*.o
fi
for f in "$SKETCH"/ui_*.cpp "$SKETCH"/lang.cpp "$SKETCH"/ruuvi_hist.cpp "$SKETCH"/history.cpp \
         "$SKETCH"/alarms.cpp "$SKETCH"/schedule.cpp "$SKETCH"/state.cpp "$SKETCH"/victron.cpp; do
  g++ -std=gnu++17 -O0 -w $INC -c "$f" -o "$WORK/ui/$(basename "$f" .cpp).o"
done
gcc -O1 -w $INC -c "$SKETCH/font_latin1.c" -o "$WORK/ui/font_latin1.o"
gcc -O1 -w $INC -c "$SKETCH/font_clock_96.c" -o "$WORK/ui/font_clock_96.o"
g++ -std=gnu++17 -O0 -w $INC -o "$WORK/shots" "$HERE/shots.cpp" "$WORK"/ui/*.o "$WORK/liblvgl.a" -lm
mkdir -p "$HERE/out_en" "$HERE/out_sv"
"$WORK/shots" en "$HERE/out_en"
"$WORK/shots" en "$HERE/out_en" alarm
"$WORK/shots" sv "$HERE/out_sv"
python3 "$HERE/topng.py" "$HERE/out_en"
python3 "$HERE/topng.py" "$HERE/out_sv"
echo "Screenshots in $HERE/out_en and $HERE/out_sv (sheet.png shows them all)"
