# 0002. Firmware platform

Status: accepted, 2026-11-09

## Context
- LilyGoLib 0.3.0 needs Arduino-ESP32 ≥ 3.3. LilyGO's tested set for the T-Watch S3 (`LilyGoLib/docs/third_party.md`):
  LVGL 9.6.0, NimBLE-Arduino 2.5.1, SensorLib 0.5.0, RadioLib 7.7.1.
- The official `LilyGoLib-PlatformIO` template is outdated (`espressif32@6.12.0`, Arduino 2.x, LilyGoLib ^0.2).
- Touch is an **FT6336U** on its own bus `Wire1`, without a reset pin: once put to sleep it only comes back after a reboot.
- Accelerometer: BMA423 or BMA456 depending on the batch; LilyGoLib detects the model at boot.

## Decision
- PlatformIO + **pioarduino** (Arduino-ESP32 ≥ 3.3) + LilyGoLib 0.3.0 + the versions above, pinned exactly.
- Board revision with the SX1262 radio (`-D ARDUINO_LILYGO_LORA_SX1262`).
- Touch always on (light sleep with touch wake ≈ 2.4 mA per LilyGO).
- Steps and wrist-tilt through our own `motion` service on top of the detected accelerometer.

## Learned during M1 (2026-11-11)
- pioarduino needs Python ≤ 3.14, so PlatformIO runs as `uvx --python 3.13 --from platformio pio`.
- LDF does not give the framework WiFi library the include path of `Network` — added to `build_flags`.
- Own light sleep with RTC-domain wake lines (TP_INT low, PMU_INT low, SENSOR_INT high, 60 s timer): LilyGoLib's
  `lightSleep()` on the S3 cannot wake on the accelerometer and turns BLE off.
- LilyGoLib enables any-motion/tap/step/activity interrupts by default — we disable them and keep only the wake
  gesture, otherwise the watch wakes all the time.
- Hourly battery log in NVS (48 entries), downloadable over HTTP: USB serial drops out in light sleep, and with USB
  connected the watch is charging anyway.

## OTA (2026-11-14)
- Cable-free development: on a charger the watch joins Wi-Fi and serves HTTP (`twatch.local`); uploads with
  `firmware/tools/ota_upload.sh`. On battery Wi-Fi is off.
- Own partition table: 2 × 4 MB app slots (ota_0/ota_1) + 7.9 MB FAT + coredump.
- Rollback without bootloader support: "pending" flag in NVS, the image must run 30 s (loop watchdog armed),
  after 3 failed boots `esp_ota_set_boot_partition(previous slot)`. Does not catch crashes before `setup()`.

## Power management (2026-11-16)
- `custom_sdkconfig` (pioarduino hybrid compile) enables `CONFIG_PM_ENABLE`, tickless idle and BT controller modem
  sleep, so BLE can stay connected while the CPU light-sleeps (see [ble.md](../ble.md)). One build environment `twatch`.
