#pragma once
#include <stdint.h>

class NimBLEServer;

// Meshtastic phone API over the watch's NimBLE server (the same link the iPhone uses for ANCS), so the official
// Meshtastic app can configure the node and chat. Port of Meshtastic's NimbleBluetooth.cpp (GPL-3.0) to
// NimBLE-Arduino 2.x: BLE callbacks only queue data, PhoneAPI runs in the mesh task.
namespace lora {

void addPhoneService(NimBLEServer *server); // from the BLE worker before advertising, only when LoRa is on
const char *phoneServiceUuid();
void phoneLinkUp(uint16_t connHandle);      // encrypted link up (from the BLE worker callbacks)
void phoneLinkDown();
void startPhoneApi();                       // in the mesh task, after the core is up

} // namespace lora
