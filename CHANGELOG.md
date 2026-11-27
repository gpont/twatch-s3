# Changelog

All notable changes to this project. Dates are release dates of the firmware build.

## [Unreleased]
- LoRa: Meshtastic-compatible mesh node on the built-in SX1262 (vendored Meshtastic 2.8.3 core), controlled from the
  official Meshtastic iPhone app over the watch's Bluetooth link; bottom screen with on/off, status and messages.
- License changed from MIT to GPL-3.0 (required by the Meshtastic code).
- Notifications: action buttons with the labels iOS sends; notifications delivered quietly / into the Scheduled
  Summary are skipped.
- Settings: switch off changing the face by long press.
- Diagnostics: hourly power stats (`/stats`), BLE connection parameters in the log, mesh log (`/meshlog`).

## [0.1.0] - 2026-11-18
First public release.
- 7 watch faces: Big digits, Game Boy, Dashboard, Analog, Terminal, 90s LCD, Word clock.
- iPhone notifications over BLE (ANCS, no app): list of 20, full-screen card, swipe to dismiss (synced
  to the iPhone), incoming call decline/accept, per-app vibration, quiet nights, unread counter, emoji.
- Hardware step counter with a 7-day chart; RTC + NTP time with 12 time zones.
- Light sleep with tap / crown / wrist wake; power-manager sleep with BLE; night power save.
- Settings on the watch.
- Wi-Fi OTA updates with automatic rollback; HTTP diagnostics (screenshots, logs, hourly power stats,
  crash dumps).
