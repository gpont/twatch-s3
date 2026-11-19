#pragma once
#include <stdint.h>

// User settings persisted in NVS (namespace "settings").
struct Settings {
    uint8_t brightness = 1;     // index into kBrightnessLevels (50/255: see docs/power)
    uint8_t timeoutSec = 7;     // screen auto-off
    uint16_t stepGoal = 8000;
    uint8_t face = 0;           // M2
    bool gestureWake = true;    // wrist gesture wakes the screen
    bool tapWake = true;        // tap on the screen wakes it
    uint8_t vibration = 2;      // 0 soft, 1 medium, 2 strong
    uint8_t timezone = 0;       // index into local_time::kZones
    bool notifications = true;
    bool nightSave = true;
    bool lora = false;          // Meshtastic-compatible LoRa node (src/lora), applied at boot      // night power save 22:30-07:30 (app/modes)  // iPhone notifications over BLE (+ PM auto light sleep)
};

namespace settings {

constexpr uint8_t kBrightnessLevels[] = {20, 50, 90, 150, 230};
constexpr uint8_t kTimeouts[] = {5, 7, 10, 15};

void load();
void save();
Settings &get();
uint8_t backlight(); // PWM value for the current brightness index

} // namespace settings
