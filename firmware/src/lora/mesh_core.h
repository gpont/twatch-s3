#pragma once
#include <stddef.h>
#include <stdint.h>
#include <Print.h>

// Meshtastic-compatible LoRa node on the built-in SX1262 (vendored core in lib/meshtastic, GPL-3.0).
// Runs the core's cooperative OSThread scheduler in its own FreeRTOS task. Started at boot only when
// Settings -> LoRa is on (the core has no clean shutdown: switching it off reboots the watch).
namespace lora {

void begin();          // start the core (call after the watch hardware is up)
bool running();
uint32_t nodeCount();  // nodes in the NodeDB (including us)
uint32_t messages();   // text messages received since boot
bool lastMessage(char *from, size_t fromLen, char *text, size_t textLen); // false if none yet
bool takeNewMessage(); // true once per new text message (the loop vibrates / wakes)
const char *status();  // short text for the UI / diagnostics
void dumpLog(::Print &out); // the core's own log (RAM ring, GET /meshlog)

} // namespace lora
