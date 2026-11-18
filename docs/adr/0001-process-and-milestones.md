# 0001. Process and milestones

Status: accepted, 2026-11-09

## Decision
- Monorepo: `firmware/` (PlatformIO), `docs/`, `scripts/`. A companion server is planned but not part of the firmware.
- Milestones: **M0** flash backup + accelerometer model check → **M1.5** watch-face concept gallery → **M1** base watch
  (time, sleep, steps) → **M2** faces, navigation, settings, power optimizations → **M3** iPhone notifications →
  M4 optional companion server. M1.5 went before M1 because the font set (sizes, glyphs) depends on the chosen faces.
- **Gate:** nothing is flashed before a verified full flash backup exists (16 MB, size and checksum checked, kept outside the repo).
- After each milestone: how to flash, what to check on hardware, what is risky. Nothing counts as working on the device
  until it was verified on a real watch.
- Pure logic is test-driven with host tests (`pio test -e native`); UI is verified with on-device screenshots
  (`GET /screenshot`).
