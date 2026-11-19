# Meshtastic inside this firmware — feasibility study

Status: research, 2026-11-19. Not implemented. Source studied: `meshtastic/firmware` @ `647831b` (2.8.3, 2026-11-17).

## Goal
Keep this firmware (faces, iPhone notifications, sleep model, settings) and add a Meshtastic node on the
built-in SX1262 radio, controlled from the official **Meshtastic iOS app** over Bluetooth — the way external LoRa
modules work. Approach "B2": take the Meshtastic mesh core into our firmware, not our features into Meshtastic.

## Findings

### Licensing
Meshtastic firmware is **GPL-3.0**. Using its code means this project becomes GPL-3.0 (owner agrees; no outside
contributors yet, so relicensing is possible). The Meshtastic name and logo fall under their trademark guidelines:
say "compatible with Meshtastic", do not brand the firmware as Meshtastic.

### Same toolchain — the good news
- Meshtastic's ESP32 builds use **the same platform we use**: pioarduino `55.03.312-1` (Arduino-ESP32 3.3, IDF 5.5)
  with a `custom_sdkconfig` hybrid compile. No platform conflict.
- The T-Watch S3 is an officially supported Meshtastic target (`variants/esp32s3/t-watch-s3`, hw model 51,
  `T_WATCH_S3`). Pins confirmed: LoRa SCK 3, MISO 4, MOSI 1, CS 5, RESET 8, DIO1 (IRQ) 9, BUSY 7, DIO2 as RF switch,
  TCXO 1.8 V.
- Meshtastic already has **compile-time switches for a stripped build** (`configuration.h`):
  `MESHTASTIC_EXCLUDE_SCREEN`, `_GPS`, `_WIFI`, `_MQTT`, `_POWER_FSM`, `_POWERMON`, `_BLUETOOTH`, `_MODULES` and
  per-module ones (`_RANGETEST`, `_STOREFORWARD`, `_ATAK`, `_CANNEDMESSAGES`, …).
- The core is covered by host tests (`test/`: routing, NodeDB, PhoneAPI, crypto, packet history), so the core
  already compiles without the hardware layer on the Linux "portduino" target. Separability is real, not theoretical.
- LilyGoLib can leave the radio alone: `LilyGoDeviceInitOptions::initRadio = false`.

### Size of what we would take
- `src/mesh/` ≈ 32 k lines, of which the parts we need: `Router` + `FloodingRouter` + `NextHopRouter` +
  `ReliableRouter`, `MeshService`, `NodeDB` (5 k lines on its own), `PhoneAPI` (2.5 k), `Channels`, `CryptoEngine`
  (+ PKI/XEdDSA), `RadioInterface` + `RadioLibInterface` + `SX126xInterface`/`SX1262Interface`, `MeshPacketQueue`,
  `PacketHistory`, generated protobufs (`src/mesh/generated`, nanopb).
- Minimal modules the phone app expects: `RoutingModule`, `NodeInfoModule`, `AdminModule`, `TextMessageModule`
  (Position/Telemetry optional).
- Support code: `concurrency/` (OSThread cooperative scheduler on ArduinoThread), `Observer`, `FSCommon` +
  `SafeFile` (LittleFS), `meshUtils`, `Throttle`, `UptimeClock`, `gps/RTC` (time), `memGet`.
- Libraries: nanopb 0.4.92, meshtastic/Crypto (AES-CTR, Curve25519), meshtastic/ArduinoThread, ErriezCRC32,
  RadioLib at a pinned commit.
- Whole Meshtastic ESP32-S3 binaries are ~2 MB; the core subset is estimated at **+0.8–1.2 MB** flash. We use
  2.5 MB of a 4 MB app slot, so it fits, but the emoji fonts (570 KB) may need to move to the FAT partition later.

### Where it does not fit as-is
| Conflict | Meshtastic | Ours | Plan |
| --- | --- | --- | --- |
| BLE library | Arduino-ESP32 `BLEDevice` (NimBLE backend), `src/nimble/NimbleBluetooth.cpp`, ignores NimBLE-Arduino | NimBLE-Arduino 2.5.1 (ANCS client) | Do not take their BLE file. Re-implement the Meshtastic GATT service (`6ba1b218-15a8-461f-9fa8-5dcae273eafd`: ToRadio / FromRadio / FromNum, take the UUIDs from their source) on our NimBLE server and feed it to `PhoneAPI`, which is transport-independent. Same link serves ANCS and the Meshtastic app. |
| Main loop | `setup()`/`loop()` own everything, OSThread scheduler, `runASAP` | our loop + PM auto sleep | Run their OSThread scheduler in a dedicated FreeRTOS task; init only the objects we need (`nodeDB`, `router`, `service`, modules). Their globals from `main.cpp` (`router`, `nodeDB`, `service`, `rebootAtMsec`, …) get defined in a small glue file. |
| Storage | LittleFS (`/prefs/*.proto`, NodeDB) behind the `FSCom` macro (`FSCommon.h`) | FAT partition `ffat` (7.9 MB, unused) | Point `FSCom` at `FFat` for our build: same Arduino `FS` API (open, mkdir, remove, rename; `SafeFile` writes a temp file and renames). No partition change, no USB flash. Trade-off: FAT survives power loss mid-write worse than LittleFS (writes are rare: config changes, NodeDB saves). |
| Radio IRQ wake | GPIO interrupt on DIO1 | BLE wakes the CPU from inside the chip (the BT controller is part of the ESP32-S3 and the power manager schedules its events). The SX1262 is an external chip: it signals a packet on pin DIO1, and an external pin can end light sleep only via ext0/ext1 on this board (`gpio_wakeup` did not work in manual sleep). ext1 is "any low" (touch, crown), ext0 is the accelerometer (active high) | DIO1 is active high and cannot join ext1. Our auto-sleep loop already wakes every 100 ms to check touch/crown/gesture, so it checks DIO1 too — no extra wakeups. The SX1262 keeps a received packet in its buffer with DIO1 high until read; LoRa packets take hundreds of ms on air. Risk: two packets within 100 ms → the second is lost. To try in phase 1: `gpio_wakeup` under PM auto sleep (only manual sleep was tested). |
| Power manager | Meshtastic's own PowerFSM and light-sleep logic | our PM locks and sleep model | `MESHTASTIC_EXCLUDE_POWER_FSM`; our `power` service stays in charge. |
| RadioLib version | pinned commit | 7.7.1 (LilyGoLib headers include it) | Use one version for both; check LilyGoLib builds with Meshtastic's commit. |
| RTC / time | `gps/RTC.h`, PCF8xRTC lib | our clock service (PCF8563 via SensorLib) | Stub their RTC to read/write our clock. |

### Power
- SX1262 in continuous receive ≈ 4.6–5.3 mA (datasheet), that is **+30–35 %** on top of today's ~15 mA in
  notification mode → roughly 22–23 h per charge. Meshtastic clients normally listen all the time.
- Hence LoRa must be a **setting (off by default)**, matching the owner's "LoRa on demand" idea. Night power save
  turns it off too.
- Transmit (≈ 100 mA for ~0.5–1 s per message at default LongFast) is negligible for chat use.

### Testing
- Receiving needs **other Meshtastic nodes in range**: a public mesh nearby or a second device. Without one, only the
  phone-app side (BLE, config, own node info) can be tested.
- The iOS app checks the firmware version and hardware model it reports; we should report a recent version and
  `T_WATCH_S3`.

## Recommended plan (if we go)
| Phase | Content | Done when | Estimate |
| --- | --- | --- | --- |
| 0 | Relicense to GPL-3.0; vendor Meshtastic at a pinned tag (git subtree under `firmware/lib/meshtastic`), libraries in `platformio.ini`, `FSCom` → FFat | firmware builds with the core compiled in, nothing started | 1 day |
| 1 | Glue: globals, variant, RTC stub, OSThread task, `initRadio=false`, radio power; start NodeDB/Router/MeshService/minimal modules on the default LongFast channel | log shows received packets / own NodeInfo sent | 2–4 days |
| 2 | Meshtastic GATT service on our NimBLE server → `PhoneAPI`; coexistence with ANCS | iOS Meshtastic app connects, reads config, sends and receives messages | 2–3 days |
| 3 | Watch side: **mesh screen below the face** (the free tile): LoRa on/off, link and node count, channel, last messages; DIO1 polling in auto sleep, haptics, night save integration, power check with `/stats` | messages on the wrist, measured drain | 2–4 days |

Total ≈ **1.5–2.5 weeks** of focused work, plus testing rounds on the wrist.

## Main risks
1. Coupling inside NodeDB/PhoneAPI is larger than the include lists suggest (config, modules, file paths): the glue
   grows. Mitigation: phase 1 is the go/no-go — stop if the core does not start with a reasonable amount of glue.
2. Upstream moves fast; we pin a version and update deliberately.
3. Flash space (fonts + core) and RAM for NodeDB (PSRAM is available, 8 MB).
4. A missed packet while two arrive within one 100 ms poll.

## Alternative kept in mind
- **MeshCore** (MIT, simpler protocol, own iOS app, lists the T-Watch as a target) — smaller mesh community.
- **Meshtastic firmware as-is** in the second OTA slot for quick experiments (range, app) without integration.
