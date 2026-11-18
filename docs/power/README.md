# T-Watch S3 power consumption: measurements and conclusions

State as of 2026-11-17. Hardware: T-Watch S3 (SX1262), 470 mAh battery.
Related: [dev-workflow.md](../dev-workflow.md) (sleep, hardware quirks), [ble.md](../ble.md) (notifications).
Raw measurement logs are not published; the profiler firmware that produced them was removed after the runs.

## 1. Summary

**What draws how much** (best estimate and honest range):

| State | ~mA | Range | Confidence |
| --- | --- | --- | --- |
| Light sleep (screen off; touch/crown/gesture wake, 60 s timer) | **6.8** | 5–9 | medium: 4 h phase, one method |
| CPU awake at 240 MHz, screen off | **~50** | 32–70 | low (methods disagree 2×) |
| Screen on, backlight 90/255, LVGL loop without pauses, 240 MHz | **~85** | 79–90 | medium (two runs agree) |
| Screen on, backlight 230/255 | **~115** | 105–137 | medium |
| Wi-Fi connected, modem sleep, screen off | ≈ awake + 0…5 | — | relative |
| Wi-Fi without power save | ≈ ×1.7 vs modem sleep | — | relative |
| **Notifications on (BLE + PM auto sleep), normal day of wear** | **~15** (avg) | — | one day of real use, 2026-11-17 |

**Battery life** (470 mAh):

| Scenario | Average | Lasts |
| --- | --- | --- |
| Sleep only (lying on a table) | 6.8 mA | ~69 h |
| Sleep + screen 5 min/h | ~13 mA | ~36 h |
| Notifications on, real wear (measured) | ~15 mA | ~30 h |

**Biggest levers** (by effect):
1. **Sleep current.** LilyGO reports ~2.4 mA for light sleep with touch wake; we measured 6.8 mA. The watch sleeps most
   of the day, so this matters most.
2. **Screen with a busy LVGL loop at 240 MHz.** A 5 ms pause ≈ −16 %, plus 80 MHz ≈ −10 %.
3. **Brightness:** backlight 20 → 230 ≈ +60 % with the screen on.
4. **Idle CPU clock:** 240 → 80 MHz ≈ −35 %.
5. Wi-Fi in modem sleep (≈ −40 % vs power save off). For BLE: long connection intervals.

## 2. Why the numbers are imprecise (method)
- **The AXP2101 does not measure current** (SensorLib: `VBUS_CURRENT and BAT_CURRENT are not supported`). Only battery
  voltage and the fuel-gauge percentage are available.
- A profiler firmware held one state for a fixed time on battery and logged mV and % every minute; current was derived
  from how much was used per phase.
- **Traps found:**
  1. **The fuel gauge sits at 100 % for 1.5–2 h after charging,** then catches up (−12 % in 30 min with almost flat
     voltage). Percent is only usable for the total of a whole run.
  2. **The voltage slope depends on where you are on the discharge curve**: fast at 4.3–4.0 V, a plateau around
     3.8 V. Absolute mA "from the slope" are biased: too high early, too low late.
  3. **Relaxation:** after a heavy phase the voltage rises for minutes even under load. The first 3 minutes of each
     phase were dropped, but on the plateau that is not enough.
- Two conversions were used: linear (mV/min × duration normalized to the run's total Δ% × 4.7 mAh) and via a typical
  LiPo curve. Where they disagree 2×, the table says "low confidence".
- **Reliable:** comparisons of **adjacent phases** (same part of the curve) and states that matched in two runs.
- **Not resolvable:** differences below ~2–5 mA within 1 h (small consumers in sleep).

## 3. Runs (2026-11-15/16)

### Run 1 — firmware as it was in M1, LoRa in standby
~155 mAh (100 % → 67 %) in 6 h, no human wakes, no crashes.

| Phase | min | mV | ~mA (linear) |
| --- | --- | --- | --- |
| settle (sleep right after charging) | 10 | 4313 → 4296 | (30, relaxation) |
| CPU awake, screen off | 29 | 4296 → 4224 | 48 |
| screen, backlight 90 | 29 | 4223 → 4117 | 79 |
| screen, backlight 230 | 19 | 4105 → 4023 | 105 |
| Wi-Fi connected (modem sleep) | 29 | 4019 → 4000 | 49 |
| **light sleep** | **240** | 4016 → 3961 | **6.8** |

### Run A — big consumers, LoRa asleep
~287 mAh (100 % → 39 %) in 5.1 h. One reboot at 40 MHz (`INT_WDT`), the run resumed by itself.

| Phase | mV/min | ~mA linear | ~mA curve |
| --- | --- | --- | --- |
| idle 240 MHz | 1.98 | 70 | 32 |
| idle 160 MHz | 1.46 | 51 | 23 |
| idle 80 MHz | 1.26 | 44 | 20 |
| idle 40 MHz (**INT_WDT crash**) | 0.87 | 31 | 17 |
| 1 core busy, 80 MHz | 1.40 | 49 | 36 |
| 1 core busy, 240 MHz | 2.09 | 74 | 53 |
| 2 cores busy, 240 MHz | 2.70 | 95 | 75 |
| screen, backlight 20 | 2.10 | 74 | 60 |
| backlight 50 | 2.22 | 78 | 64 |
| backlight 90 | 2.57 | 90 | 84 |
| backlight 150 | 2.98 | 105 | 114 |
| backlight 230 | 3.37 | 119 | 137 |
| backlight 90 + 5 ms LVGL pause | 2.15 | 76 | 89* |
| same + 80 MHz | 1.93 | 68 | 108* |
| Wi-Fi modem sleep | 1.15 | 41 | 66 |
| Wi-Fi power save off | 1.97 | 69 | 113 |
| BLE advertising 100 ms (no sleep) | 1.14 | 40 | 69 |
| BLE advertising 1000 ms (no sleep) | 1.01 | 36 | 65 |

\* the curve correction on the ~3.9 V plateau overestimates these two; trust mV/min within the pair.

**Reliable conclusions from adjacent phases:**
| Comparison | Effect |
| --- | --- |
| idle 240 → 160 → 80 MHz | −26 %, then −14 % (**≈ −35 %** total) |
| 40 MHz | less, but **crashes** → minimum 80 MHz |
| 1 → 2 busy cores (240 MHz) | **≈ +30 %** |
| backlight 20 → 230 | **≈ +60 %** |
| 5 ms pause in the LVGL loop (screen on) | **≈ −16 %**, + 80 MHz another ≈ −10 % |
| Wi-Fi power save off → modem sleep | **≈ −40 %** |
| BLE advertising 100 → 1000 ms | ≈ −11 % |

### Run B — small consumers in sleep, 1 h each
Started at 38 % (~3.78 V, plateau) and resolved nothing: at that level a 1–5 mA difference over 1 h is below noise.
Small consumers (LoRa, amplifier, haptic driver, accelerometer, timer wake cost) need overnight phases above 70 %
charge or a current meter.

## 4. Datasheet reference (NOT measured)
| Component | Mode | Typical | In this firmware |
| --- | --- | --- | --- |
| ESP32-S3 | light sleep with RTC timer | ~0.2–0.3 mA | yes |
| ESP32-S3 | idle 80/240 MHz (no radio) | ~20 / ~40 mA | while the screen is on |
| SX1262 (LoRa) | standby RC / XOSC | 0.6 / 0.8 mA | — |
| SX1262 | sleep (warm start) | ~0.6 µA | `radio.sleep()` at boot |
| MAX98357A (amp) | on, silent / shutdown | ~2.4 mA / < 1 µA | not initialized |
| DRV2605 (haptics) | standby | a few µA | — |
| BMA423 | normal 100 Hz / low power | ~0.15 / ~0.014 mA | normal 100 Hz |
| FT6336U (touch) | active / monitor | a few mA / hundreds of µA | auto-monitor |
| ST7789V | sleep-in | tens of µA | `setBrightness(0)` sends SLPIN |
| PCF8563 (RTC) | running | ~0.25 µA | — |
| LilyGO, whole watch | light sleep with touch wake | **2.38 mA** (their measurement) | we measured 6.8 mA |

## 5. Optimizations applied (2026-11-16)
LVGL frame pacing, 80/240 MHz scaling, default brightness 50, LoRa sleep at boot, touch auto-monitor, audio drivers
not started, PM auto light sleep for BLE, night power save. See [ADR 0007](../adr/0007-no-pet-m2-scope.md).

## 6. Ongoing monitoring
- `GET /stats`: hourly summary — battery, mode, BLE link and interval, screen time, wakes, share of the hour in
  auto/manual light sleep, drops, notifications.
- `GET /batlog`: hourly battery percent and voltage.
- Open question: notification mode averages ~15 mA over a day instead of the expected 7–8 mA.
