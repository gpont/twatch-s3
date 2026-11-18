# Development workflow

How to build and flash (Wi-Fi and USB), the on-watch HTTP API, and the pitfalls found so far. Backup, restore
and download mode: [flashing.md](flashing.md). Decisions: [adr/](adr/).

## TL;DR
```sh
scripts/setup.sh                  # once: secrets
scripts/flash_usb.sh              # first flash over USB
firmware/tools/ota_upload.sh      # later: build + upload over Wi-Fi (watch on any charger)
curl http://twatch.local/info     # what is running now
```

## 1. Environment
- **PlatformIO with Python 3.13 through uv**: `uvx --python 3.13 --from platformio pio …`. pioarduino refuses
  Python ≥ 3.15 (`Python version must be 3.10 … 3.14`); a Homebrew `pio` may already use a newer Python.
- Platform: pioarduino `55.03.312-1` (Arduino-ESP32 3.3.12, ESP-IDF 5.5), LilyGoLib 0.3.0, LVGL 9.6.0. Versions are
  pinned in `firmware/platformio.ini`.
- **npm** (font generator only): the repo has an `.npmrc` pointing at the public registry, in case your global
  `~/.npmrc` points at a private one.
- **Secrets** (gitignored), templates next to them with `.example`, or generate with `scripts/setup.sh`:
  - `firmware/include/secrets.h`: Wi-Fi, `OTA_PASSWORD` (server fields are reserved);
  - `firmware/ota_secrets.ini`: `password` (same OTA password) and `host = twatch.local`.

## 2. Flashing over Wi-Fi (main way)
1. Put the watch on **any charger**. While VBUS is present the watch joins Wi-Fi, serves HTTP on port 80 as
   `twatch.local` and does not light-sleep (the screen still turns off). On battery Wi-Fi is off.
2. Run `firmware/tools/ota_upload.sh`. It:
   - builds the firmware (`pio run -e twatch`, same build dir as USB flashing);
   - on macOS resolves `twatch.local` once via Bonjour and then uses the IP (mDNS through curl is slow right after boot);
   - shows `/info` before, does `POST /update`, waits for the reboot and shows `/info` after.
   - `SKIP_BUILD=1` uploads the last build. The first argument is a host or IP if `twatch.local` does not resolve.
3. Check: after the upload `slot` changed (`app0` ↔ `app1`) and `pending=1`; 30 s later it must be `pending=0`.
   The build time is shown in `fw=` and in the system info under the steps screen.

### HTTP API (only while charging)

| Request | What it does |
| --- | --- |
| `GET /info` | `fw`, `slot`, `pending`, battery, uptime |
| `POST /update` | basic auth `ota:<OTA_PASSWORD>`, multipart field `firmware` → write the free slot and reboot |
| `GET /debug` | sleep/wake counters by reason, interrupt line levels, raw and daily steps, battery, BLE state, flags |
| `GET /debug?face=N` / `tile=N` / `row=N` | (auth) switch face / screen / scroll settings to row N — for screenshots |
| `GET /debug?notif=TEXT&msg=…[&call=1]` | (auth) fake notification or call (UI and haptics test without a phone) |
| `GET /debug?pm=1` | (auth) power-manager locks and time per mode |
| `GET /debug?forget=1` | (auth) delete the BLE bond |
| `GET /debug?led=0..4` | (auth) AXP2101 CHGLED: off / low / high / blink 1 Hz / blink 4 Hz (if an LED is fitted) |
| `GET /debug?sleeptest=SEC` | (auth) light-sleep even on the charger for SEC s, Wi-Fi off meanwhile |
| `GET /debug?selftest=1` | (auth) wake self-test without a human: PCF8563 timer → ext1; vibration → accelerometer any-motion → ext0. PASS/FAIL in `/log` |
| `GET /debug?wdttest=task\|rtc` | (auth) deliberate hang: checks that the watchdog reboots and the log names the stage |
| `GET /screenshot` | 240×240 BMP of the screen (turns the screen on if needed) |
| `GET /log` | diagnostic ring log (~3 KB in RTC memory: survives sleep and soft resets, not power-off) |
| `GET /coredump` | (auth) summary of the last crash: task, PC, backtrace — decode with `addr2line` |
| `GET /batlog` | hourly battery log (CSV, 48 h) |
| `GET /stats` | hourly power summary (48 h): battery, mode (BLE / night), link, screen seconds, wakes, % sleep (auto / manual), BLE interval, drops, notifications |

Manually: `curl -u ota:<password> -F firmware=@firmware/.pio/build/twatch/firmware.bin http://<ip>/update`

Decode a crash on the **same** ELF that produced it:
```sh
~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-addr2line -pfiaC \
  -e firmware/.pio/build/twatch/firmware.elf <backtrace addresses>
```

### Rollback to the previous firmware
- Partition table (`firmware/partitions.csv`): `app0` and `app1` 4 MB each, FAT 7.9 MB, coredump.
- After OTA the new image is marked `pending` in NVS. It must run **30 s** with the loop watchdog armed (a hang →
  panic → reboot). If it fails 3 boots in a row, `ota::guardEarly()` switches back (`esp_ota_set_boot_partition`).
- Stock bootloader; `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is not needed.
- **New image hangs with a dark screen:** power-cycle with the crown (6 s off, 2 s on), up to 3 times, and it rolls back.
- **Not caught:** a crash before `setup()` (static init), and an image that runs but broke Wi-Fi/OTA — then use USB.

## 3. Flashing over USB (first time and recovery)
```sh
scripts/flash_usb.sh
# or: uvx --python 3.13 --from platformio pio run -d firmware -e twatch -t upload
```
- Port `/dev/cu.usbmodem*` (macOS) / `/dev/ttyACM*` (Linux). No port → download mode ([flashing.md](flashing.md)).
- esptool resets the watch via RTS after flashing; the screen may just be off (tap, crown).
- Back up with `scripts/backup-flash.sh` before the first flash.
- **Do not combine `-t erase -t upload`** in one command: the build then failed in LDF (`FFat.h`, `Wire.h` not found).

## 4. Build time — do not break the cache
| Situation | Time |
| --- | --- |
| First build (downloads ESP-IDF, rebuilds the framework — hybrid compile) | ~10 min |
| Full rebuild (~1100 files: LVGL, Arduino core, NimBLE, LilyGoLib) | ~7 min |
| One file changed | ~3.5 min (mostly LDF `deep+` dependency scanning) |
| Upload over Wi-Fi | ~30–40 s (2.5 MB image) |

What triggers a **full** rebuild:
- **any change to `firmware/platformio.ini`** — batch such edits;
- **changing values in `build_flags`** such as `${UNIX_TIME}` — that is why the build time goes into the generated
  `src/build_info.h` (`tools/build_info.py`), included only by `ota.cpp`;
- **a new library `#include`** — in LDF `deep+` it changes the include path list of every compile command;
- **a separate upload environment** — each env has its own `.pio/build/<env>`, so OTA is a script on top of `twatch`.

## 5. macOS: Local Network privacy
On recent macOS the *Local Network* privacy setting can block non-Apple binaries (Homebrew/uv Python) from reaching
the LAN (`OSError: [Errno 65] No route to host`), while Apple's own tools (`/usr/bin/curl`, `ping`, `ssh`) work.
That is why OTA is plain HTTP uploaded with `/usr/bin/curl` instead of ArduinoOTA/espota (which also needs a
reverse TCP connection from the watch). esptool over USB is not affected.

## 6. Serial log
- Opening the port resets the watch (`rst:0x15 USB_UART_CHIP_RESET`), even with `dtr=False, rts=False`.
- In light sleep USB serial drops out; a live log works only while the watch is on USB and awake (charging).
  The HTTP `/log` is usually more convenient.
- `uvx --python 3.13 --from platformio pio device monitor -d firmware` (115200).

## 7. T-Watch S3 and LilyGoLib quirks — verified on the device

### Waking from light sleep
| Source | Pin | Level | Mechanism | Status |
| --- | --- | --- | --- | --- |
| Touch FT6336U | TP_INT 16 | active low | ext1 `ANY_LOW` + `rtc_gpio_pullup_en` | ✅ on the wrist |
| Crown (AXP2101 PEK) | PMU_INT 21 | active low | ext1 `ANY_LOW` + `rtc_gpio_pullup_en` | ✅ on the wrist |
| Accelerometer BMA423 (wrist tilt) | SENSOR_INT 14 | active high | ext0, level 1 | ✅, but not every gesture (fixed sensitivity) |
| RTC PCF8563 | RTC_INT 17 | active low | ext1 | ✅ self-test |
| Timer | — | — | `esp_sleep_enable_timer_wakeup` (60 s; 10 min in night save) | ✅ |

- **`gpio_wakeup_enable()` does not wake this board from light sleep at all**, even with `gpio_sleep_sel_dis()`.
  Only the RTC path (ext0/ext1) works, as in `LilyGoWatch2022::lightSleep()`.
- **`gpio_wakeup_enable()` switches the pin interrupt to level-triggered.** LilyGoLib attaches edge ISRs (TP and
  PMU falling, SENSOR rising). Left level-triggered, an active line re-fires the ISR forever and the loop starves:
  screen on, time frozen. After every wake the edge types are restored (`restoreEdgeInterrupts()`).
- **ext1 on the ESP32-S3 is only "any low" or "any high" for the whole mask**, so the active-high accelerometer uses ext0.
- **Do not make accelerometer INT1 active-low.** SensorLib then switches it to open-drain, and SENSOR_INT has a
  pull-down on the board, so it reads "active" and every sleep is rejected instantly (returns in 3–5 ms, cause 0).
- After ext0 the pad stays in the RTC mux: `rtc_gpio_deinit(SENSOR_INT)` gives it back to the digital GPIO/ISR.
- **Rejected sleep** shows in `/log` as `wake: cause=0` right after `pre-sleep lines active: …`.
- In IDF 5 `esp_sleep_enable_ext1_wakeup_io()` **adds** pins to the mask: call `esp_sleep_disable_ext1_wakeup_io(0)` first.

### Accelerometer
- Model depends on the batch: **BMA423** (hardware wrist tilt) or BMA456H (no wrist feature in SensorLib → double
  tap fallback). LilyGoLib detects it at boot.
- LilyGoLib enables any-motion, tap, step and activity interrupts on INT1 by default; `motion::begin()` disables them.
- The step counter is enabled last (after platform and tilt config). The hardware counter is cumulative; per-day
  accounting lives in `lib/core/steps_day`.
- Platform `WRISTBAND` is needed for wrist tilt. The BMA423 tilt feature has no tunable sensitivity.

### Display and touch
- `instance.sleepDisplay()` / `wakeupDisplay()` are empty stubs, but `setBrightness(0)` (`LilyGoDispSPI`) sends
  ST7789 `SLPIN`, and a non-zero brightness afterwards sends `SLPOUT`.
- FT6336U has no reset pin: never put it to sleep, only a reboot brings it back. While the screen is off and the
  watch is awake (charging), taps are detected by polling TP_INT.
- In auto-sleep mode touch is switched to polling mode (`G_MODE` 0xA4 = 0) so TP_INT stays low for a whole touch.

### RTC PCF8563
- SensorLib `setCountdownTimer()` masks the frequency with `& TIMER_TD10` and **drops the TE bit** — the timer never
  starts. Registers are written directly (`0x0E = 0x82`, `0x0F = N`, `0x01 = TIE`), see `power.cpp` (self-test).
- The RTC stores UTC.

### Power management
- Audio (I2S) must stay uninitialized: its drivers hold an `APB_FREQ_MAX` lock forever and light sleep never
  happens (`init.initAudio = false`).
- `setCpuFrequencyMhz()` fights the power manager — use PM locks (`power::setFastCpu`).
- NimBLE blocking calls wait on task notifications: never wake a task that makes GATT calls with `xTaskNotifyGive`.

### Other
- **LDF:** the framework WiFi library does not get the `Network` include path (added in `build_flags`); WebServer
  and FS dependencies are pulled by explicit `#include`s; `lib_ldf_mode = deep+` is required.
- RadioLib cannot be removed: `LilyGoWatchS3.h` includes it. Only the SX126x driver is kept (`RADIOLIB_EXCLUDE_*`).

## 8. Watchdogs and stage breadcrumbs
- Loop watchdog always on (8 s, panic + reboot). It is unsubscribed around `esp_light_sleep_start()` (the loop
  legitimately blocks there), fed manually during OTA upload.
- An RTC watchdog runs during light sleep too (75 s; ~10 min in night save).
- The main loop and the sleep/wake path write a stage marker (`diag::stage`) to RTC memory. After a watchdog reset
  or panic `/log` shows `!!! previous run died in stage '<stage>'`.
- A full power-off with the crown clears RTC memory: if the watch hangs, let the watchdog reboot it.

## 9. Power measurements
The AXP2101 cannot measure current, so power is estimated from voltage/percent over time — see
[power/README.md](power/README.md). Ongoing monitoring: `GET /stats` (hourly) and `GET /batlog`.

## 10. Hybrid compile (`custom_sdkconfig` in `env:twatch`) — pitfalls
- First build ~10 min (downloads ESP-IDF, rebuilds ~3000 files), then ~3–7 min.
- **pioarduino replaces the shared core** `~/.platformio/packages/framework-arduinoespressif32-libs` with the rebuilt
  libraries. Other projects using the same platform get the PM-enabled core too.
- The build drops helper files into `firmware/` (`managed_components/`, `sdkconfig.*`, `.dummy/`,
  `dependencies.lock`, `CMakeLists.txt`) — gitignored. **Check `git status` before `git add`.**
- The first run may fail with `FileNotFoundError … .sconsign313.tmp` after copying libraries — just run it again.
- `open_memstream` does not work in this newlib — use `funopen` to capture `esp_pm_dump_locks()`.
