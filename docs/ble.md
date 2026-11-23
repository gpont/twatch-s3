# iPhone notifications over BLE (ANCS)

## How it works
iPhone notifications come over **ANCS** (Apple Notification Center Service), which is built into iOS — no app is
needed. The watch is a BLE peripheral: it advertises an ANCS *service solicitation*, the user pairs it once in
*Settings → Bluetooth*, and then the watch acts as a GATT **client** of the iPhone and subscribes to its
Notification Source, Data Source and Control Point characteristics.

The link has to stay up all the time. If the CPU could not sleep while connected, the watch would draw ~50 mA
(≈ 10 h of battery). So the first step of this milestone was an experiment: can the ESP32-S3 keep the BLE link
while it is in automatic light sleep? Answer: **yes** (below).

## Why the framework is rebuilt
The prebuilt Arduino core (pioarduino 55.03.312-1) has `CONFIG_PM_ENABLE` and `CONFIG_BT_CTRL_MODEM_SLEEP` off.
A manual `esp_light_sleep_start()` cannot keep a BLE connection; the ESP-IDF power manager can (automatic light
sleep with tickless idle, the BT controller wakes itself for its connection events).
`firmware/platformio.ini` rebuilds ESP-IDF and the core via `custom_sdkconfig`:
```
CONFIG_PM_ENABLE, CONFIG_PM_PROFILING, CONFIG_FREERTOS_USE_TICKLESS_IDLE (IDLE_TIME_BEFORE_SLEEP=3),
CONFIG_BT_CTRL_MODEM_SLEEP + MODE_1, CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL + MAIN_XTAL_PU_DURING_LIGHT_SLEEP
```
The board has no 32 kHz crystal for the ESP32, so BLE in sleep is clocked from the main crystal.
Hybrid-compile pitfalls: [dev-workflow.md §11](dev-workflow.md).

## The experiment (2026-11-16)
1. **No pairing → the iPhone drops the link** after 70–110 s (reason `0x213`, remote user terminated). iOS keeps a
   link only with a *paired* accessory that actually uses ANCS. Fix: request security right after connecting,
   subscribe to ANCS after encryption (GATT calls are made outside NimBLE callbacks).
2. **Link stable for 30+ min, notifications arrive — but the CPU never slept.** The PM lock dump
   (`esp_pm_dump_locks`) showed two `i2s_driver` locks holding `APB_FREQ_MAX` 99 % of the time: LilyGoLib starts the
   speaker amplifier and the PDM microphone drivers. Fix: `instance.begin()` with
   `initAudio = false` (audio is unused). GPS/NFC/SD do not exist on this model and are skipped too.
3. **Result:**

| Minute | Light sleep | Sleep entries | Rejects | Link |
| --- | --- | --- | --- | --- |
| 1 | 42.6 % (includes start-up) | 424 | 0 | up |
| 2 | **87.2 %** | 931 | 0 | up |
| 3 | **90.3 %** | 1233 | 0 | up, 0 drops |

The remaining ~10 % awake time is mostly I2C polling (LilyGoLib `instance.loop()`) and RTOS ticks.

Side findings: `open_memstream` does not work in this newlib (use `funopen` to capture `esp_pm_dump_locks`), and
PM stats rows look like `SLEEP     80 M  12345  12 %` (frequency printed with a space before `M`). A crash
(`MDNS.end()` after Wi-Fi was already off) was found with `GET /coredump` + `addr2line`; the network is now torn
down in order HTTP → mDNS → Wi-Fi.

## Implementation
One build (`twatch`). Settings → *Notifications* switches live:

| Notifications | Sleep model | BLE |
| --- | --- | --- |
| off | manual `esp_light_sleep_start()` while the screen is off | off (`NimBLEDevice::deinit`) |
| on | PM auto light sleep between ticks and BLE events | advertises as `twatch`, keeps the iPhone link |

The night power save (22:30–07:30, optional) temporarily switches to the "off" row.

- **PM is always configured** (80–240 MHz). 240 MHz comes from a `CPU_FREQ_MAX` lock while touching (instead of
  `setCpuFrequencyMhz`, which fights the power manager). Screen on or Wi-Fi work → `NO_LIGHT_SLEEP` lock.
- **Waking in auto mode** (`power.cpp`, `arm()/autoIdle()`): ext1 (PMU_INT, TP_INT) and ext0 (SENSOR_INT) only end
  a sleep; the loop checks the line levels every 100 ms. Touch is switched to polling mode (`G_MODE 0xA4 = 0`) while
  armed so TP_INT stays low for the whole touch — in trigger mode it only pulses and a 100 ms poll would miss it. The
  crown is read from the AXP2101 IRQ status. Background work (steps, NTP) every 60 s.
- **`notify.cpp`**: NimBLE and all GATT calls live in a worker task `ancs` (GATT calls block; the UI must not wait).
  NimBLE callbacks only queue packets. Finished notifications go through a queue to the loop → `ancs::Store`
  (last 20, sorted by uid) → UI and haptics.
  - The worker is woken with a **semaphore, not a task notification**: NimBLE's blocking GATT calls wait on the
    calling task's notification bits, so an `xTaskNotifyGive` ends a write early and the reply later lands on a dead
    stack (this caused a boot loop on connect).
- **Protocol** (`lib/core/ancs.*`, host-tested): Notification Source → queue of uids (newest 20) → Control Point
  `GetNotificationAttributes` (app id, title ≤ 95 B, message ≤ 399 B, date) → Data Source reply reassembled from
  fragments → app name from a table of known bundle ids or `GetAppAttributes` (cached). Request timeout 15 s
  (a 400-byte reply at MTU 23 and a 330 ms interval takes ~8 s).
- **Connection parameters**: 5 s after connecting the watch asks for 300–330 ms, latency 4, timeout 6 s (within
  Apple's accessory guidelines). Logged when the iPhone applies them.
- On every connection the list is cleared and refilled by iOS replaying the *PreExisting* notifications (removals
  while disconnected are never sent).
- **Text** is filtered to what the fonts can draw: Latin + Latin-1/Extended-A, all Cyrillic, quotes, single emoji
  (monochrome Noto Emoji `f_emoji_*` as fallback of the text fonts, +570 KB flash). Flags, skin tones, ZWJ and
  variation selectors are dropped (a ZWJ sequence shows as its parts).
- **Silent notifications** (iOS `Silent` flag: delivered quietly or into the Scheduled Summary) are not fetched
  or shown at all — they stay in the iPhone's summary.
- **UX** ([ADR 0005](adr/0005-ux-behaviour.md)): vibration per app; a new notification wakes the screen and shows a
  card; swipe or *Delete* = ANCS negative action. Calls: decline/accept screen, buzz every 1.5 s, max 30 s. At night
  only calls vibrate. *Unread* counter on the Big digits face.
- **Debug**: `/debug?notif=TEXT&msg=…[&call=1]` fake notification; `/debug?pm=1` PM locks and time per mode;
  `/debug?forget=1` delete the bond (also "Forget This Device" on the iPhone); `/stats` hourly power summary.

## Open questions
- Measured draw with notifications on is ~15 mA average over a day (expected 7–8 mA). Being investigated with the
  hourly stats: sleep share, connection interval, drops, screen time.
- Wi-Fi is not used in auto mode: on a charger and during NTP the watch holds `NO_LIGHT_SLEEP`.
