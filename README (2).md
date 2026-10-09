<!-- esp32-S3-ws4-caravan v1.0 -->
# Screenshots

The images in the main README are rendered by the firmware's own UI code: the `ui_*.cpp`
files, the language files and fonts, compiled for a PC with LVGL 8.4 and given example
data. What you see is what the board draws, pixel for pixel, apart from the clock and
date, which come from the PC.

```
sh tools/screenshots/build.sh
```

The first run downloads LVGL 8.4 and builds it (a minute or two). The images end up in
`tools/screenshots/out_en/` and `out_sv/`, with `sheet.png` showing them all. Copy the ones
you want over the `screen-*.png` files in the sketch folder.

| File | What it is |
|---|---|
| `shots.cpp` | Stand-ins for the hardware, the example data, and which screens are shot |
| `stub/` | Small versions of the Arduino, WiFi and SD headers, just enough to compile the UI |
| `topng.py` | Turns the raw frames into PNG files (needs numpy and Pillow) |
| `build.sh` | All of the above in one go |

Change the example data in `example_data()` in `shots.cpp`.
