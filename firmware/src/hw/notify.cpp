#include "notify.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>
#include <time.h>
#include "app/settings.h"
#include "diag.h"
#include "power.h"
#include "lora/phone_ble.h"

namespace notify {

// ANCS (Apple Notification Center Service) UUIDs
static const char *kAncsService = "7905F431-B5CE-4E99-A40F-4B1E122D00D0";
static const char *kNotificationSource = "9FBF120D-6301-42D9-8C58-25E699A21DBD";
static const char *kDataSource = "22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB";
static const char *kControlPoint = "69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9";

// NimBLE host task -> worker: raw GATT notifications
struct Packet { uint8_t src; uint8_t len; uint8_t data[244]; }; // src 0 = Notification Source, 1 = Data Source
// worker -> loop
enum OutKind : uint8_t { kNew, kUpdate, kRemove, kClear };
struct Out { OutKind kind; ancs::Item item; };
// loop -> worker
enum CmdKind : uint8_t { kAction, kForget };
struct Cmd { CmdKind kind; uint32_t uid; ancs::Action action; };

static QueueHandle_t s_in, s_out, s_cmd;
static TaskHandle_t s_worker, s_loopTask;
// Wakes the worker. NOT a task notification: NimBLE's blocking GATT calls wait on the calling task's
// notification bits, so an xTaskNotifyGive would end a write early and the reply then hits a dead stack.
static SemaphoreHandle_t s_wake;
static volatile bool s_want = false;
static volatile Link s_link = Link::kOff;
static volatile bool s_needSecurity = false, s_needSubscribe = false, s_paramsPending = false, s_dropped = false;
static volatile uint16_t s_handle = 0;
static volatile uint32_t s_connectedAt = 0;
static Stats s_stats = {};

static void noteParams(const NimBLEConnInfo &ci, const char *what)
{
    s_stats.intervalX100us = ci.getConnInterval() * 125 / 10;
    s_stats.latency = ci.getConnLatency();
    s_stats.timeoutMs = ci.getConnTimeout() * 10;
    diag::log("ble: %s interval=%u.%u ms latency=%u timeout=%u ms", what, s_stats.intervalX100us / 10,
              s_stats.intervalX100us % 10, s_stats.latency, s_stats.timeoutMs);
}

static void wakeWorker() { if (s_wake) xSemaphoreGive(s_wake); }

// ================= worker side =================
static bool s_running = false;
static NimBLERemoteCharacteristic *s_cp = nullptr;
static ancs::Assembler s_asm;

struct Pend { uint32_t uid; uint8_t flags, category; bool announce; };
static Pend s_pend[ancs::Store::kCapacity]; // highest uids only, unsorted
static int s_pendCount = 0;
static bool s_inflight = false;
static uint32_t s_inflightAt = 0;
static Pend s_cur;
static ancs::Attributes s_waitAttr; // got the notification, asking the app name
static bool s_waitingName = false;

struct AppName { char id[ancs::kAppMax + 1]; char name[ancs::kNameMax + 1]; };
static AppName s_names[12];
static int s_namesNext = 0;

class ServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *, NimBLEConnInfo &ci) override
    {
        s_handle = ci.getConnHandle();
        s_connectedAt = millis();
        s_link = Link::kConnected;
        s_needSecurity = true; // iOS keeps the link only for a paired accessory
        s_paramsPending = true;
        noteParams(ci, "connected,");
        // LoRa on: keep advertising so the Meshtastic app can find the watch while iOS holds the ANCS link
        if (settings::get().lora) NimBLEDevice::startAdvertising();
        wakeWorker();
    }
    void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override
    {
        s_link = Link::kWaiting;
        s_dropped = true;
        s_stats.drops++;
        s_stats.intervalX100us = 0;
        if (settings::get().lora) lora::phoneLinkDown();
        diag::log("ble: disconnected reason=0x%x after %lus", reason, (unsigned long)((millis() - s_connectedAt) / 1000));
        wakeWorker();
    }
    void onConnParamsUpdate(NimBLEConnInfo &ci) override { noteParams(ci, "params updated:"); }
    void onAuthenticationComplete(NimBLEConnInfo &ci) override
    {
        diag::log("ble: auth encrypted=%d bonded=%d", ci.isEncrypted(), ci.isBonded());
        if (ci.isEncrypted()) s_needSubscribe = true;
        if (ci.isEncrypted() && settings::get().lora) lora::phoneLinkUp(ci.getConnHandle());
        wakeWorker();
    }
};

static void onPacket(uint8_t src, const uint8_t *d, size_t n)
{
    static Packet p; // host task only
    p.src = src;
    p.len = n < sizeof(p.data) ? n : sizeof(p.data);
    memcpy(p.data, d, p.len);
    if (xQueueSend(s_in, &p, 0) != pdTRUE) diag::log("ancs: inbound queue full");
    wakeWorker();
}

static void advertise()
{
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData data;
    data.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    data.setName("twatch");
    // AD type 0x15: 128-bit service solicitation = ANCS (little endian), lets iOS offer the ANCS link
    static const uint8_t ancsSol[] = {17, 0x15, 0xD0, 0x00, 0x2D, 0x12, 0x1E, 0x4B, 0x0F, 0xA4,
                                      0x99, 0x4E, 0xCE, 0xB5, 0x31, 0xF4, 0x05, 0x79};
    data.addData(ancsSol, sizeof(ancsSol));
    adv->setAdvertisementData(data);
    if (settings::get().lora) { // Meshtastic app scans for its service: 128-bit UUID goes into the scan response
        NimBLEAdvertisementData scan;
        scan.addServiceUUID(NimBLEUUID(lora::phoneServiceUuid()));
        adv->setScanResponseData(scan);
        adv->enableScanResponse(true);
    }
    adv->setMinInterval(800); // 500-600 ms: cheap, iOS still reconnects within seconds
    adv->setMaxInterval(960);
    adv->start();
}

static void putOut(OutKind k, const ancs::Item *it)
{
    static Out o; // worker only
    o.kind = k;
    if (it) o.item = *it; else memset(&o.item, 0, sizeof(o.item));
    if (xQueueSend(s_out, &o, pdMS_TO_TICKS(500)) != pdTRUE) diag::log("ancs: loop queue full, dropped");
    if (s_loopTask) xTaskNotifyGive(s_loopTask);
}

static void resetSession()
{
    s_cp = nullptr;
    s_asm.reset();
    s_inflight = false;
    s_waitingName = false;
    s_pendCount = 0;
}

static void startBle()
{
    NimBLEDevice::init("twatch");
    NimBLEDevice::setSecurityAuth(true, false, true); // bonding + secure connections, "Just Works"
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCb());
    server->advertiseOnDisconnect(true);
    if (settings::get().lora) lora::addPhoneService(server);
    advertise();
    s_running = true;
    s_link = Link::kWaiting;
    diag::log("ble: on, advertising");
}

static void stopBle()
{
    resetSession();
    NimBLEDevice::deinit(true);
    s_running = false;
    s_link = Link::kOff;
    putOut(kClear, nullptr);
    diag::log("ble: off");
}

static void subscribe()
{
    NimBLEClient *client = NimBLEDevice::getServer()->getClient(s_handle);
    NimBLERemoteService *svc = client ? client->getService(kAncsService) : nullptr;
    if (!svc) { diag::log("ancs: service not found"); return; }
    NimBLERemoteCharacteristic *ns = svc->getCharacteristic(kNotificationSource);
    NimBLERemoteCharacteristic *ds = svc->getCharacteristic(kDataSource);
    NimBLERemoteCharacteristic *cp = svc->getCharacteristic(kControlPoint);
    if (!ns || !ds || !cp) { diag::log("ancs: characteristic missing"); return; }
    // Data Source first: subscribing to Notification Source makes iOS replay everything pre-existing
    bool okDs = ds->subscribe(true, [](NimBLERemoteCharacteristic *, uint8_t *d, size_t n, bool) { onPacket(1, d, n); });
    putOut(kClear, nullptr); // the replay below rebuilds the list (removals while away were never sent)
    s_cp = cp;
    bool okNs = ns->subscribe(true, [](NimBLERemoteCharacteristic *, uint8_t *d, size_t n, bool) { onPacket(0, d, n); });
    if (okDs && okNs) s_link = Link::kReady;
    diag::log("ancs: subscribed ns=%d ds=%d mtu=%u", okNs, okDs, client->getMTU());
}

static void addPending(const ancs::SourceEvent &e)
{
    Pend p = {e.uid, e.flags, e.category, e.event == ancs::kAdded && !(e.flags & ancs::kPreExisting)};
    for (int i = 0; i < s_pendCount; i++)
        if (s_pend[i].uid == e.uid) { s_pend[i] = p; return; }
    if (s_pendCount < (int)(sizeof(s_pend) / sizeof(s_pend[0]))) { s_pend[s_pendCount++] = p; return; }
    int lo = 0; // full: keep the newest
    for (int i = 1; i < s_pendCount; i++)
        if (s_pend[i].uid < s_pend[lo].uid) lo = i;
    if (s_pend[lo].uid < e.uid) s_pend[lo] = p;
}

static void dropPending(uint32_t uid)
{
    for (int i = 0; i < s_pendCount; i++)
        if (s_pend[i].uid == uid) { s_pend[i] = s_pend[--s_pendCount]; return; }
}

static const char *cachedName(const char *id)
{
    if (const char *k = ancs::knownAppName(id)) return k;
    for (auto &n : s_names)
        if (n.id[0] && !strcmp(n.id, id)) return n.name;
    return nullptr;
}

static void emit(const ancs::Attributes &a, const char *name)
{
    static ancs::Item it; // worker only
    memset(&it, 0, sizeof(it));
    it.uid = a.uid;
    it.category = s_cur.category;
    it.flags = s_cur.flags;
    it.haptic = ancs::hapticFor(a.app, s_cur.category);
    if (!ancs::formatTime(a.date, it.when)) {
        time_t t = time(nullptr);
        struct tm lt;
        localtime_r(&t, &lt);
        snprintf(it.when, sizeof(it.when), "%02d:%02d", lt.tm_hour, lt.tm_min);
    }
    if (!name || !*name) { // fallback: last part of the bundle id
        const char *dot = strrchr(a.app, '.');
        name = dot ? dot + 1 : a.app;
    }
    strncpy(it.app, name, sizeof(it.app) - 1);
    strncpy(it.title, a.title, sizeof(it.title) - 1);
    strncpy(it.message, a.message, sizeof(it.message) - 1);
    strncpy(it.positive, a.positive, sizeof(it.positive) - 1);
    strncpy(it.negative, a.negative, sizeof(it.negative) - 1);
    // what iOS offers per app (research: can app actions like Okta "Approve" be pressed from the watch?)
    diag::log("ancs: %s cat=%u flags=0x%02x pos='%s' neg='%s'", a.app, it.category, it.flags, a.positive, a.negative);
    putOut(s_cur.announce ? kNew : kUpdate, &it);
}

static bool writeCp(const uint8_t *b, size_t n)
{
    if (!s_cp || s_link != Link::kReady) return false;
    return s_cp->writeValue(b, n, true);
}

static void onAttributes(const ancs::Attributes &a)
{
    const char *name = cachedName(a.app);
    if (!name && a.app[0]) {
        uint8_t b[ancs::kAppMax + 4];
        size_t n = ancs::buildGetAppName(a.app, b, sizeof(b));
        s_waitAttr = a;
        s_waitingName = true;
        s_asm.expectAppName(a.app);
        s_inflightAt = millis();
        if (n && writeCp(b, n)) return; // stays in flight until the name arrives
        s_asm.reset();
        s_waitingName = false;
    }
    emit(a, name);
    s_inflight = false;
}

static void onPacketWorker(const Packet &p)
{
    if (p.src == 0) {
        ancs::SourceEvent e;
        if (!ancs::parseSource(p.data, p.len, e)) return;
        if (e.event == ancs::kRemoved) {
            dropPending(e.uid);
            ancs::Item it = {};
            it.uid = e.uid;
            putOut(kRemove, &it);
        } else {
            addPending(e);
        }
        return;
    }
    switch (s_asm.feed(p.data, p.len)) {
    case ancs::Assembler::kNotification: onAttributes(s_asm.attributes()); break;
    case ancs::Assembler::kAppName: {
        AppName &slot = s_names[s_namesNext];
        s_namesNext = (s_namesNext + 1) % (int)(sizeof(s_names) / sizeof(s_names[0]));
        strncpy(slot.id, s_asm.appId(), sizeof(slot.id) - 1);
        strncpy(slot.name, s_asm.appName(), sizeof(slot.name) - 1);
        if (s_waitingName) emit(s_waitAttr, slot.name);
        s_waitingName = false;
        s_inflight = false;
        break;
    }
    case ancs::Assembler::kError:
        diag::log("ancs: bad response for uid %lu", (unsigned long)s_cur.uid);
        s_inflight = false;
        s_waitingName = false;
        break;
    default: break;
    }
}

static void pump()
{
    // a 400-byte reply at MTU 23 is ~25 packets; with the slow 330 ms interval that can take ~8 s
    if (s_inflight && millis() - s_inflightAt > 15000) {
        diag::log("ancs: timeout uid %lu", (unsigned long)s_cur.uid);
        s_asm.reset();
        if (s_waitingName) emit(s_waitAttr, nullptr);
        s_waitingName = false;
        s_inflight = false;
    }
    if (s_inflight || !s_pendCount || s_link != Link::kReady) return;
    int hi = 0; // newest first
    for (int i = 1; i < s_pendCount; i++)
        if (s_pend[i].uid > s_pend[hi].uid) hi = i;
    s_cur = s_pend[hi];
    s_pend[hi] = s_pend[--s_pendCount];
    uint8_t b[16];
    size_t n = ancs::buildGetNotification(s_cur.uid, b, sizeof(b));
    s_asm.expectNotification(s_cur.uid);
    s_inflight = true;
    s_inflightAt = millis();
    if (!writeCp(b, n)) {
        s_asm.reset();
        s_inflight = false;
    }
}

static void worker(void *)
{
    for (;;) {
        xSemaphoreTake(s_wake, pdMS_TO_TICKS(1000));
        if (s_want && !s_running) startBle();
        if (!s_want && s_running) stopBle();
        if (s_dropped) { s_dropped = false; resetSession(); }
        if (!s_running) {
            xQueueReset(s_in);
            xQueueReset(s_cmd);
            continue;
        }
        if (s_link >= Link::kConnected && s_needSecurity) {
            s_needSecurity = false;
            int rc = 0;
            bool ok = NimBLEDevice::startSecurity(s_handle, &rc);
            diag::log("ble: security ok=%d rc=%d", ok, rc);
        }
        if (s_link >= Link::kConnected && s_needSubscribe) {
            s_needSubscribe = false;
            subscribe();
        }
        if (s_link >= Link::kConnected && s_paramsPending && millis() - s_connectedAt > 5000) {
            s_paramsPending = false;
            // 300-330 ms, latency 4, 6 s timeout: iOS-legal and cheap (docs/ble.md)
            NimBLEDevice::getServer()->updateConnParams(s_handle, 240, 264, 4, 600);
            diag::log("ble: asked for 300-330 ms, latency 4");
        }
        static Packet p;
        while (xQueueReceive(s_in, &p, 0) == pdTRUE) onPacketWorker(p);
        Cmd c;
        while (xQueueReceive(s_cmd, &c, 0) == pdTRUE) {
            if (c.kind == kForget) {
                NimBLEDevice::deleteAllBonds();
                if (s_link >= Link::kConnected) NimBLEDevice::getServer()->disconnect(s_handle);
                diag::log("ble: bonds deleted");
            } else {
                uint8_t b[8];
                size_t n = ancs::buildAction(c.uid, c.action, b, sizeof(b));
                bool ok = writeCp(b, n);
                diag::log("ancs: action %u on %lu ok=%d", c.action, (unsigned long)c.uid, ok);
            }
        }
        pump();
    }
}

// ================= loop side =================
static ancs::Store s_store;

void begin()
{
    s_loopTask = xTaskGetCurrentTaskHandle();
    s_in = xQueueCreate(24, sizeof(Packet));
    s_out = xQueueCreate(6, sizeof(Out));
    s_cmd = xQueueCreate(4, sizeof(Cmd));
    s_wake = xSemaphoreCreateBinary();
    s_want = settings::get().notifications;
    xTaskCreate(worker, "ancs", 6144, nullptr, 2, &s_worker);
}

void setEnabled(bool on)
{
    s_want = on;
    wakeWorker();
}

Link link() { return s_link; }

const char *linkText()
{
    switch (s_link) {
    case Link::kOff: return "выкл";
    case Link::kWaiting: return "нет связи";
    case Link::kConnected: return "сопряжение";
    default: return "подключено";
    }
}

Events tick()
{
    Events ev;
    static Out o;
    static ancs::Item arrived;
    while (xQueueReceive(s_out, &o, 0) == pdTRUE) {
        ev.changed = true;
        switch (o.kind) {
        case kNew:
            s_stats.received++;
            o.item.unread = true;
            s_store.put(o.item);
            arrived = o.item;
            ev.arrived = &arrived;
            break;
        case kUpdate: {
            s_stats.received++;
            const ancs::Item *old = s_store.find(o.item.uid);
            o.item.unread = old && old->unread;
            s_store.put(o.item);
            break;
        }
        case kRemove:
            if (ev.arrived && ev.arrived->uid == o.item.uid) ev.arrived = nullptr;
            if (const ancs::Item *it = s_store.find(o.item.uid)) {
                if (it->category == ancs::kIncomingCall) ev.callEnded = true;
            }
            s_store.remove(o.item.uid);
            break;
        case kClear: s_store.clear(); break;
        }
    }
    return ev;
}

const ancs::Store &store() { return s_store; }
const Stats &stats() { return s_stats; }
void markRead(uint32_t uid) { s_store.markRead(uid); }
void markAllRead() { s_store.markAllRead(); }

void dismiss(uint32_t uid)
{
    const ancs::Item *it = s_store.find(uid);
    if (it && (it->flags & ancs::kNegativeAction)) {
        Cmd c = {kAction, uid, ancs::kActionNegative};
        xQueueSend(s_cmd, &c, 0);
        wakeWorker();
    }
    s_store.remove(uid);
}

void act(uint32_t uid, bool positive)
{
    if (!positive) { dismiss(uid); return; }
    Cmd c = {kAction, uid, ancs::kActionPositive};
    xQueueSend(s_cmd, &c, 0);
    wakeWorker(); // iOS then sends "removed" if the action consumed the notification
}

void callAnswer(uint32_t uid, bool accept)
{
    Cmd c = {kAction, uid, accept ? ancs::kActionPositive : ancs::kActionNegative};
    xQueueSend(s_cmd, &c, 0);
    wakeWorker();
}

void inject(const char *title, const char *message, bool call)
{
    static uint32_t uid = 0x7F000000;
    ancs::Item it = {};
    it.uid = ++uid;
    it.category = call ? ancs::kIncomingCall : ancs::kSocial;
    it.flags = ancs::kNegativeAction | (call ? ancs::kPositiveAction : 0);
    it.haptic = call ? ancs::Haptic::kCall : ancs::Haptic::kTelegram;
    strcpy(it.when, "12:34");
    strcpy(it.app, call ? "Телефон" : "Telegram");
    if (!call) { strcpy(it.positive, "Approve"); strcpy(it.negative, "Deny"); it.flags |= ancs::kPositiveAction; }
    ancs::sanitize(title, strlen(title), it.title, sizeof(it.title), false);
    ancs::sanitize(message, strlen(message), it.message, sizeof(it.message), true);
    static Out o; // loop side: not the worker's buffer
    o.kind = kNew;
    o.item = it;
    xQueueSend(s_out, &o, 0);
}

void forgetPhone()
{
    Cmd c = {kForget, 0, ancs::kActionNegative};
    xQueueSend(s_cmd, &c, 0);
    wakeWorker();
}

} // namespace notify
