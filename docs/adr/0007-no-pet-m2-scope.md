# 0007. Everyday-watch scope, navigation and power optimizations

Status: accepted, 2026-11-16. Replaces parts of ADR 0005 and ADR 0006.

## Scope
- A virtual-pet feature from the original plan was dropped. The face that depended on it was removed; the Dashboard
  shows a notifications tile instead, Game Boy shows pixel weather.
- Until a companion server exists, budget and weather show "—".

## Navigation (updated 2026-11-17)
Around the face: **notifications up**, **settings left**, **steps right** with **system info** below on the same
screen (scroll down: battery, iPhone link, firmware, OTA). No screen below the face. Swipe back to the face from the
side screens. Next face: long press anywhere on the face (also a settings row).
Faces: Big digits, Game Boy, Dashboard, Analog, Terminal, 90s LCD, Word clock.

## On-watch settings
Brightness (5 levels, default 50/255), screen timeout, step goal, notifications on/off, night power save, wrist
wake, tap wake, vibration strength (3 levels), face, time zone, time sync now, reboot.
Wrist-gesture sensitivity: the BMA423 tilt feature has no tunable thresholds — would need own tilt detection.

## Optimizations from measurements ([power report](../power/README.md))
1. The LVGL loop renders on its own timer and yields the CPU in between (≈ −16 % with the screen on).
2. Dynamic CPU clock: 80 MHz while the screen is static, 240 MHz during touch/animation + ~1.5 s (≈ −10…35 %).
   Never below 80 MHz (INT_WDT crash at 40 MHz).
3. Default brightness 50/255.
4. LoRa radio put to sleep at boot (−0.6…0.8 mA per datasheet).
5. Touch in auto-monitor mode (drops to low power by itself and still wakes on touch). Full touch sleep is forbidden.
6. Lower accelerometer rate — **postponed**: BMA423 features (steps, wrist tilt) may need a fixed rate; risk of
   breaking steps for ~0.1 mA.
