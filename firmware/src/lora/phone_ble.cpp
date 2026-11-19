// Meshtastic phone API over NimBLE-Arduino 2.x. Ported from meshtastic/firmware src/nimble/NimbleBluetooth.cpp
// (GPL-3.0): same queues and threading rules — BLE callbacks run in the NimBLE host task and only move bytes
// through mutex-protected queues; getFromRadio/handleToRadio run in the mesh task (OSThread runOnce).
#include "phone_ble.h"
#include <NimBLEDevice.h>
#include <array>
#include <atomic>
#include <mutex>
#include "configuration.h"
#include "BluetoothCommon.h"
#include "concurrency/OSThread.h"
#include "mesh/PhoneAPI.h"
#include "meshUtils.h"
#include "hw/diag.h"

namespace lora {

static constexpr size_t kQueue = 3;
static NimBLECharacteristic *s_fromNum = nullptr;
static std::atomic<uint16_t> s_conn{BLE_HS_CONN_HANDLE_NONE};

struct Blob {
    uint8_t data[MAX_TO_FROM_RADIO_SIZE];
    size_t len = 0;
};

class MeshPhoneAPI : public PhoneAPI, public concurrency::OSThread
{
public:
    MeshPhoneAPI() : concurrency::OSThread("PhoneBLE") { api_type = TYPE_BLE; }

    std::mutex fromPhoneMutex;
    std::atomic<size_t> fromPhoneSize{0};
    std::array<Blob, kQueue> fromPhone{};

    std::mutex toPhoneMutex;
    std::atomic<size_t> toPhoneSize{0};
    std::array<std::array<uint8_t, meshtastic_FromRadio_size>, kQueue> toPhone{};
    std::array<size_t, kQueue> toPhoneLen{};
    std::atomic<bool> readWaiting{false};

    void resetQueues()
    {
        {
            std::lock_guard<std::mutex> g(fromPhoneMutex);
            fromPhoneSize = 0;
        }
        readWaiting = false;
        {
            std::lock_guard<std::mutex> g(toPhoneMutex);
            toPhoneSize = 0;
        }
    }

protected:
    int32_t runOnce() override
    {
        while (hasWorkToPhone() || fromPhoneSize > 0) {
            handleFromPhone(); // writes before reads: clients write wantConfig and read right after
            handleToPhone();
        }
        return INT32_MAX; // woken by the BLE callbacks
    }

    void onConfigStart() override
    {
        uint16_t c = s_conn.load();
        if (c != BLE_HS_CONN_HANDLE_NONE) NimBLEDevice::getServer()->updateConnParams(c, 6, 12, 0, 600); // fast sync
    }

    void onConfigComplete() override
    {
        // back to the watch's low-power link (300-330 ms, latency 4) instead of Meshtastic's 30-50 ms
        uint16_t c = s_conn.load();
        if (c != BLE_HS_CONN_HANDLE_NONE) NimBLEDevice::getServer()->updateConnParams(c, 240, 264, 4, 600);
        diag::log("lora: phone app synced");
    }

    void onNowHasData(uint32_t fromRadioNum) override
    {
        PhoneAPI::onNowHasData(fromRadioNum);
        if (!s_fromNum) return;
        uint8_t v[4];
        put_le32(v, fromRadioNum);
        s_fromNum->setValue(v, sizeof(v));
        s_fromNum->notify();
    }

    bool checkIsConnected() override { return s_conn.load() != BLE_HS_CONN_HANDLE_NONE; }

private:
    bool canPreload()
    {
        if (!isConnected() || isSendingPackets()) return false; // in SEND_PACKETS only answer real reads
        return toPhoneSize < kQueue;
    }
    bool hasWorkToPhone() { return readWaiting || canPreload(); }

    void handleToPhone()
    {
        if (!(readWaiting || canPreload())) return;
        uint8_t buf[meshtastic_FromRadio_size];
        size_t n = getFromRadio(buf);
        if (n && toPhoneSize < kQueue) {
            std::lock_guard<std::mutex> g(toPhoneMutex);
            size_t i = toPhoneSize.load();
            memcpy(toPhone[i].data(), buf, n);
            toPhoneLen[i] = n;
            toPhoneSize++;
        }
        readWaiting = false; // only after the push
    }

    void handleFromPhone()
    {
        if (!fromPhoneSize) return;
        Blob b;
        {
            std::lock_guard<std::mutex> g(fromPhoneMutex);
            b = fromPhone[0];
            for (size_t i = 1; i < fromPhoneSize; i++) fromPhone[i - 1] = fromPhone[i];
            if (fromPhoneSize > 0) fromPhoneSize--;
        }
        handleToRadio(b.data, b.len);
    }
};

static std::atomic<MeshPhoneAPI *> s_api{nullptr};
static uint8_t s_lastToRadio[MAX_TO_FROM_RADIO_SIZE];

static void wake(MeshPhoneAPI *api)
{
    api->setIntervalFromNow(0);
    concurrency::mainDelay.interrupt();
}

class ToRadioCb : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override
    {
        MeshPhoneAPI *api = s_api.load();
        if (!api) return;
        NimBLEAttValue v = c->getValue();
        size_t len = v.length() < MAX_TO_FROM_RADIO_SIZE ? v.length() : MAX_TO_FROM_RADIO_SIZE;
        if (!memcmp(s_lastToRadio, v.data(), len)) return; // duplicate write (clients may retry)
        if (api->fromPhoneSize >= kQueue) { diag::log("lora: phone write dropped (queue full)"); return; }
        memcpy(s_lastToRadio, v.data(), len);
        {
            std::lock_guard<std::mutex> g(api->fromPhoneMutex);
            Blob &b = api->fromPhone[api->fromPhoneSize];
            memcpy(b.data, v.data(), len);
            b.len = len;
            api->fromPhoneSize++;
        }
        wake(api);
    }
};

class FromRadioCb : public NimBLECharacteristicCallbacks
{
    void onRead(NimBLECharacteristic *c, NimBLEConnInfo &) override
    {
        MeshPhoneAPI *api = s_api.load();
        if (!api) { c->setValue((const uint8_t *)"", 0); return; }
        if (api->toPhoneSize == 0) {
            // Zero-size reads break clients during config: wait (up to ~20 s) for the mesh task to fill the queue
            api->readWaiting = true;
            for (int tries = 0; api->readWaiting && tries < 4000; tries++) {
                wake(api);
                if (!api->readWaiting) break;
                delay(tries < 20 ? 1 : 5);
            }
        }
        uint8_t buf[meshtastic_FromRadio_size];
        size_t n = 0;
        {
            std::lock_guard<std::mutex> g(api->toPhoneMutex);
            size_t sz = api->toPhoneSize.load();
            if (sz) {
                n = api->toPhoneLen[0];
                memcpy(buf, api->toPhone[0].data(), n);
                for (size_t i = 1; i < sz; i++) {
                    memcpy(api->toPhone[i - 1].data(), api->toPhone[i].data(), api->toPhoneLen[i]);
                    api->toPhoneLen[i - 1] = api->toPhoneLen[i];
                }
                api->toPhoneSize--;
            }
        }
        c->setValue(buf, n);
        if (api->toPhoneSize < kQueue) wake(api); // preload the next one
    }
};

static ToRadioCb s_toRadioCb;
static FromRadioCb s_fromRadioCb;

void addPhoneService(NimBLEServer *server)
{
    NimBLEService *svc = server->createService(MESH_SERVICE_UUID);
    // Encrypted access only: the watch is bonded to the iPhone already (ANCS pairing)
    NimBLECharacteristic *toRadio =
        svc->createCharacteristic(TORADIO_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC);
    NimBLECharacteristic *fromRadio =
        svc->createCharacteristic(FROMRADIO_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC);
    s_fromNum = svc->createCharacteristic(FROMNUM_UUID,
                                          NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
    toRadio->setCallbacks(&s_toRadioCb);
    fromRadio->setCallbacks(&s_fromRadioCb);
    uint8_t zero[4] = {0};
    s_fromNum->setValue(zero, sizeof(zero));
    svc->start();
}

const char *phoneServiceUuid() { return MESH_SERVICE_UUID; }

void phoneLinkUp(uint16_t connHandle)
{
    s_conn = connHandle;
    if (MeshPhoneAPI *api = s_api.load()) wake(api);
}

void phoneLinkDown()
{
    s_conn = BLE_HS_CONN_HANDLE_NONE;
    memset(s_lastToRadio, 0, sizeof(s_lastToRadio));
    if (MeshPhoneAPI *api = s_api.load()) {
        api->resetQueues();
        api->close();
    }
}

void startPhoneApi()
{
    if (!s_api.load()) s_api = new MeshPhoneAPI();
}

} // namespace lora
