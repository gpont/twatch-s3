// Symbols the Meshtastic core expects from parts we do not build: Meshtastic's main.cpp (globals, device
// metadata), sleep.cpp / PowerFSM (the watch firmware owns sleep and power), the BLE layer and the screen.
// Derived from meshtastic/firmware 2.8.3 (GPL-3.0).
#include "configuration.h"
#include "main.h"
#include "sleep.h"
#include "PowerFSM.h"
#include "PowerStatus.h"
#include "NodeStatus.h"
#include "BluetoothStatus.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "modules/SerialModule.h"
#include "meshUtils.h"
#include "target_specific.h"
#include <RadioLib.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_mac.h>

// ---- globals normally defined in Meshtastic's main.cpp ----
Router *router = nullptr;
RadioLibHal *RadioLibHAL = nullptr;
meshtastic::PowerStatus *powerStatus = new meshtastic::PowerStatus();
meshtastic::NodeStatus *nodeStatus = new meshtastic::NodeStatus();
meshtastic::BluetoothStatus *bluetoothStatus = new meshtastic::BluetoothStatus();
ScanI2C::DeviceAddress screen_found = ScanI2C::ADDRESS_NONE;
uint32_t rebootAtMsec = 0;
uint32_t shutdownAtMsec = 0;
bool runASAP = false;
bool pauseBluetoothLogging = false;
bool pmu_found = true;

// ---- sleep / power: owned by the watch firmware (src/hw/power.cpp) ----
Observable<void *> preflightSleep;
Observable<void *> notifyDeepSleep;
Observable<void *> notifyReboot;
FakeFsm powerFSM;
bool shouldWakeOnReceivedMessage() { return false; } // the watch decides what wakes the screen
void updateBatteryLevel(uint8_t) {}                  // BLE battery service: not ours to update here

// ---- serial module is excluded ----
bool serialConfigIsValid(const meshtastic_ModuleConfig_SerialConfig &) { return true; }

// ---- hardware identity (from platform/esp32/main-esp32.cpp) ----
void getMacAddr(uint8_t *dmac) { esp_efuse_mac_get_default(dmac); }

bool getDeviceId(uint8_t *deviceId)
{
    uint32_t uniqueId[4];
    if (esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID, uniqueId, sizeof(uniqueId) * 8) != ESP_OK) return false;
    memcpy(deviceId, uniqueId, sizeof(uniqueId));
    return true;
}

// ---- device metadata for the phone app (from Meshtastic's main.cpp) ----
meshtastic_DeviceMetadata getDeviceMetadata()
{
    meshtastic_DeviceMetadata m = meshtastic_DeviceMetadata_init_default;
    strncpy(m.firmware_version, optstr(APP_VERSION), sizeof(m.firmware_version));
    m.device_state_version = DEVICESTATE_CUR_VER;
    m.canShutdown = false;
    m.hasBluetooth = true; // the watch firmware serves the phone API over its own NimBLE server
    uint8_t ble[6];
    if (esp_read_mac(ble, ESP_MAC_BT) == ESP_OK) formatMacAddr(ble, m.ble_address, sizeof(m.ble_address));
    m.hasWifi = false;
    m.hasEthernet = false;
    m.role = config.device.role;
    m.position_flags = config.position.position_flags;
    m.hw_model = HW_VENDOR;
    m.hasRemoteHardware = false;
    m.excluded_modules = meshtastic_ExcludedModules_REMOTEHARDWARE_CONFIG | meshtastic_ExcludedModules_AUDIO_CONFIG |
                         meshtastic_ExcludedModules_MQTT_CONFIG | meshtastic_ExcludedModules_NEIGHBORINFO_CONFIG |
                         meshtastic_ExcludedModules_STOREFORWARD_CONFIG | meshtastic_ExcludedModules_CANNEDMSG_CONFIG |
                         meshtastic_ExcludedModules_EXTNOTIF_CONFIG | meshtastic_ExcludedModules_DETECTIONSENSOR_CONFIG |
                         meshtastic_ExcludedModules_SERIAL_CONFIG | meshtastic_ExcludedModules_PAXCOUNTER_CONFIG |
                         meshtastic_ExcludedModules_AMBIENTLIGHTING_CONFIG;
    return m;
}
