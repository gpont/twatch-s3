# Firmware

PlatformIO + pioarduino (Arduino-ESP32 3.3.12, ESP-IDF 5.5 with power management) + LilyGoLib 0.3.0 + LVGL 9.6
+ NimBLE-Arduino 2.5.1. Start with the [project README](../README.md); details in
[../docs/dev-workflow.md](../docs/dev-workflow.md).

## Build / test / flash
```sh
PIO="uvx --python 3.13 --from platformio pio"   # pioarduino needs Python 3.10-3.14
$PIO run -d firmware                      # build (env twatch)
$PIO test -d firmware -e native           # host unit tests (pure logic in lib/core)
../scripts/flash_usb.sh                   # flash over USB
tools/ota_upload.sh                       # build + flash over Wi-Fi (watch on a charger, same network)
```

## Layout
| Path | What |
| --- | --- |
| `src/main.cpp` | setup, main loop, notification handling |
| `src/hw/power.*` | screen, light sleep (manual and PM auto), wake lines, battery, hourly stats |
| `src/hw/notify.*` | BLE peripheral + ANCS client in a worker task |
| `src/hw/ota.*` | Wi-Fi on the charger, HTTP API, OTA with rollback |
| `src/hw/clock.*`, `motion.*`, `haptics.*`, `diag.*` | RTC/NTP, accelerometer and steps, DRV2605 patterns, watchdogs and RTC log |
| `src/app/settings.*`, `modes.*` | NVS settings, night window and night power save |
| `src/ui/ui.cpp` | tiles: notifications, settings, steps + system; popups |
| `src/ui/faces/` | watch faces (one file each + `faces.cpp` registry) |
| `src/ui/fonts/` | generated LVGL fonts (`tools/fonts/gen.sh`) |
| `lib/core/` | pure logic, no Arduino: ANCS protocol, steps per day, local time, battery estimate, word clock, Russian text |
| `test/` | Unity host tests for `lib/core` |
| `tools/` | `ota_upload.sh`, `build_info.py`, font generator |
| `partitions.csv` | 2 × 4 MB app slots (OTA), 7.9 MB FAT, coredump |

## Fonts
`tools/fonts/gen.sh` downloads OFL fonts from Google Fonts and converts them with lv_font_conv into `src/ui/fonts/`.
Edit the `FONTS` table (name, family, weight, size, glyph range, symbols, fallback font) and rerun;
`ONLY="f_text_14 f_emoji_14" tools/fonts/gen.sh` regenerates selected fonts.
