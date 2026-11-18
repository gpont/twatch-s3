#pragma once

// Wireless updates.
// - On a charger (VBUS present) the watch joins Wi-Fi and serves updates as "twatch.local".
//   On battery Wi-Fi stays off. HTTP on port 80: POST /update (basic auth ota:<OTA_PASSWORD>), GET /info.
//   Upload from the Mac: firmware/tools/ota_upload.sh
// - Rollback guard: after an OTA the new image must run 30 s to be accepted. If it fails to get
//   there 3 boots in a row (panic, watchdog, or the user power-cycling a hung watch), the previous
//   slot is booted again. Works with the stock bootloader (no CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE).
namespace ota {

void guardEarly();      // first thing in setup(): may switch back to the previous slot and restart
void tick();            // every loop
bool keepAwake();       // true while listening on the charger or validating a fresh image
bool networkActive();
void suspendNetwork(bool on); // stop Wi-Fi/HTTP/mDNS cleanly and keep them down (BLE test)   // Wi-Fi owned by OTA mode (others must not switch it off)
const char *status();   // short text for the Info screen
const char *version();  // build timestamp, to tell which image is running

} // namespace ota
