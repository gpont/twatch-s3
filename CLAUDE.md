# CLAUDE.md — twatch-s3

Custom firmware for the LilyGO T-Watch S3 (ESP32-S3, SX1262). Instructions for AI coding agents; humans start at
[README.md](README.md). Owner-only notes, if present, are in `CLAUDE.local.md` (gitignored) — read it too.

## Where things are
| Path | What |
| --- | --- |
| `README.md` | user-facing overview, install, customize |
| `docs/dev-workflow.md` | **read before touching the firmware**: build, OTA, rollback, HTTP API, build-cache traps, verified hardware quirks (§7), watchdogs, hybrid compile |
| `docs/ble.md` | iPhone notifications (ANCS): BLE in PM light sleep, worker task, protocol |
| `docs/power/README.md` | power measurements, method limits, optimizations |
| `docs/adr/` | decisions (later ADRs override earlier ones) |
| `docs/flashing.md` | download mode, backup / restore |
| `firmware/` | PlatformIO project: `src/hw` board services, `src/ui` screens + `src/ui/faces`, `src/app` settings/modes, `lib/core` pure logic with host tests |

## Status
- Done and verified on the wrist: sleep/wake, steps, RTC/NTP + 12 time zones, 7 faces, navigation (notifications
  up / settings left / steps + system right), settings, Wi-Fi OTA with rollback, iPhone notifications (ANCS) with
  list, card, swipe-to-dismiss, calls, per-app haptics, unread counter, emoji, night power save.
- In progress: Meshtastic core in `firmware/lib/meshtastic` (vendored, GPL-3.0, local changes marked `// twatch:`),
  glue in `firmware/src/lora/`; phases 0–1 done, phase 2 = phone API over our NimBLE (`docs/meshtastic-integration.md`).
  Mesh log: `GET /meshlog`; switch: `GET /debug?lora=0|1`.
- Open: notification-mode draw ~15 mA average (expected 7–8) — investigate with `GET /stats`; wrist-gesture
  sensitivity needs own tilt detection (BMA423 feature has no thresholds); budget/weather wait for a companion server.

## How to work here
- PlatformIO only as `uvx --python 3.13 --from platformio pio …` (pioarduino rejects Python ≥ 3.15).
- **Build right after code changes.** A watch on a charger serves HTTP as `twatch.local`: upload with
  `SKIP_BUILD=1 firmware/tools/ota_upload.sh [host]`.
- Verify UI without a human: `GET /debug?face=N|tile=N|row=N` then `GET /screenshot` (auth `ota:<OTA_PASSWORD>`
  from `firmware/ota_secrets.ini`). Diagnostics: `/info`, `/debug`, `/log`, `/stats`, `/coredump`, `/debug?pm=1`.
- A panic: `GET /coredump`, decode with `addr2line` on the **same** ELF (rebuilding overwrites it).
- Don't change `platformio.ini` casually: any change → full rebuild. A new library include rebuilds everything too.
- Pure logic goes to `firmware/lib/core` with host tests (`pio test -e native`); hardware behaviour counts as
  working only after verification on a real watch — say what is unverified.
- Never commit secrets (`firmware/include/secrets.h`, `firmware/ota_secrets.ini`) or anything from `private/`.
  The hybrid compile drops many files into `firmware/` (gitignored) — check `git status` before `git add`.
- NimBLE blocking GATT calls wait on task notifications: never `xTaskNotifyGive` a task that makes GATT calls.
- Audio (I2S) must stay uninitialized (it blocks light sleep). Use PM locks, not `setCpuFrequencyMhz`.
