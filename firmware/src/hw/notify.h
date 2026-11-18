#pragma once
#include <stdint.h>
#include "ancs.h"

// iPhone notifications over BLE (ANCS). A worker task owns NimBLE: pairing, ANCS subscription and the
// Control Point requests (GATT calls block, so the UI loop never makes them). Finished notifications
// come to the loop through a queue: call tick() from the loop.
// Requires the PM build (auto light sleep + BT modem sleep): power.cpp light-sleeps between BLE events
// while notifications are on (docs/ble.md).
namespace notify {

enum class Link : uint8_t { kOff, kWaiting, kConnected, kReady }; // Ready = paired + ANCS subscribed

void begin();              // starts BLE if settings::notifications
void setEnabled(bool on);  // live switch (the worker starts / stops NimBLE)
Link link();
const char *linkText();    // for UI/diagnostics

struct Events {
    bool changed = false;          // store changed: redraw the list
    const ancs::Item *arrived = nullptr; // a new (not pre-existing) notification to show + vibrate
    bool callEnded = false;        // the incoming call notification went away
};
Events tick();             // loop: apply worker results to the store
const ancs::Store &store();
void markRead(uint32_t uid);
void markAllRead();

void dismiss(uint32_t uid);          // ANCS negative action if allowed, and remove locally
void callAnswer(uint32_t uid, bool accept);

struct Stats {
    uint16_t intervalX100us; // current connection interval, 0.1 ms units (0 = no link)
    uint8_t latency;
    uint16_t timeoutMs;
    uint32_t drops;          // disconnects since boot
    uint32_t received;       // notifications fetched since boot
};
const Stats &stats();

void forgetPhone();                  // delete bonds (pair again from scratch)
void inject(const char *title, const char *message, bool call); // debug: fake notification

} // namespace notify
