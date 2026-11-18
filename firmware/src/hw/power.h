#pragma once
#include <stdint.h>
#include <Arduino.h>

// Screen on/off, light sleep, battery state.
//
// Two sleep models (switch: Settings -> Уведомления):
// - manual (notifications off): screen off -> esp_light_sleep_start() loop, below.
// - auto (notifications on): the ESP-IDF power manager light-sleeps between FreeRTOS ticks and BLE
//   events (BLE needs that); the loop polls the wake lines every 100 ms (docs/ble.md).
// Screen off -> light sleep loop. Wake sources: touch (TP_INT low), crown/charger (PMU_INT low),
// wake gesture (SENSOR_INT high, not at night) and a 60 s timer for background work.
// Only touch/crown/gesture turn the screen on; the timer wakes are silent.
namespace power {

struct Battery {
    uint8_t percent = 0;
    uint16_t millivolts = 0;
    bool charging = false;
    bool usb = false;
    bool low = false;           // <= 15 % and not charging
    int32_t minutesLeft = -1;   // -1 = unknown
};

void begin();
void tick();                  // call every loop
bool screenOn();
void screenOnNow();
void screenOffNow();
void userActivity();          // restart idle timer
void setAutoSleep(bool on);   // PM auto light sleep (notifications on) vs manual light sleep
bool autoSleep();
void setNightSave(bool on);   // 10 min timer wakes, dimmest backlight, 5 s screen timeout
void setFastCpu(bool on);     // 240 MHz while interacting, 80 MHz otherwise (PM lock)
void pmReport(Print &out);    // esp_pm_dump_locks: who holds PM locks, time per mode
const Battery &battery();
void dumpLog(Print &out = Serial); // hourly battery log (CSV)
void dumpStats(Print &out);        // hourly power stats (NVS ring, 48 h): GET /stats

// debug (driven from /debug over Wi-Fi)
void requestSelfTest();              // automatic wake self-test (RTC timer + haptic/accelerometer)
void setSleepTest(uint32_t seconds); // light-sleep even on the charger for N s (Wi-Fi off meanwhile)
bool sleepTestActive();
void setPanelSleep(bool on);         // ST7789 sleep-in when the screen is off
bool panelSleep();

// set by main for the sleep loop: run silent periodic work after a timer wake
void setBackgroundWork(void (*fn)());

} // namespace power
