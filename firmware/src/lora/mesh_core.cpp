// Glue between the watch firmware and the vendored Meshtastic core (lib/meshtastic): defines the globals
// Meshtastic's main.cpp normally owns and replays the parts of its setup() that the mesh needs.
#include "mesh_core.h"
#include <Arduino.h>
#include <SPI.h>
#include "configuration.h"
#include "FSCommon.h"
#include "SerialConsole.h"
#include "SPILock.h"
#include "PowerMon.h"
#include "power/PowerHAL.h"
#include "gps/RTC.h"
#include <sys/time.h>
#include "concurrency/OSThread.h"
#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "mesh/RadioInterface.h"
#include "mesh/ReliableRouter.h"
#include "modules/Modules.h"
#include "modules/TextMessageModule.h"
#include "Observer.h"
#include "airtime.h"
#include "main.h"
#include "UptimeClock.h"
#include "mesh/RadioLibInterface.h"
#include "mesh/Throttle.h"
#include "hw/diag.h"
#include "app/settings.h"
#include <Preferences.h>

namespace lora {

static TaskHandle_t s_task = nullptr;
static volatile bool s_running = false;
static volatile bool s_radioOk = false;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static char s_from[12], s_text[200];
static volatile uint32_t s_messages = 0;
static volatile bool s_new = false;

// The core logs through `console` (USB serial); keep a copy in a RAM ring for GET /meshlog.
class RingPrint : public Print {
public:
    size_t write(uint8_t c) override
    {
        if (!m_buf) m_buf = (char *)heap_caps_malloc(kSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!m_buf) return 1;
        portENTER_CRITICAL(&m_mux);
        m_buf[m_head] = (char)c;
        m_head = (m_head + 1) % kSize;
        if (m_len < kSize) m_len++;
        portEXIT_CRITICAL(&m_mux);
        return 1;
    }
    void dump(Print &out)
    {
        if (!m_buf) return;
        size_t start = (m_head + kSize - m_len) % kSize;
        for (size_t i = 0; i < m_len; i++) out.write(m_buf[(start + i) % kSize]);
    }

private:
    static constexpr size_t kSize = 8192;
    char *m_buf = nullptr;
    size_t m_head = 0, m_len = 0;
    portMUX_TYPE m_mux = portMUX_INITIALIZER_UNLOCKED;
};
static RingPrint s_log;

// Runs in the mesh task when the core hands a text message to its modules
class TextTap {
public:
    int onText(const meshtastic_MeshPacket *mp)
    {
        char from[sizeof(s_from)];
        const meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(mp->from);
        if (n && n->short_name[0]) snprintf(from, sizeof(from), "%s", n->short_name);
        else snprintf(from, sizeof(from), "!%04lx", (unsigned long)(mp->from & 0xFFFF));
        size_t len = mp->decoded.payload.size < sizeof(s_text) - 1 ? mp->decoded.payload.size : sizeof(s_text) - 1;
        portENTER_CRITICAL(&s_mux);
        memcpy(s_from, from, sizeof(s_from));
        memcpy(s_text, mp->decoded.payload.bytes, len);
        s_text[len] = 0;
        s_messages++;
        s_new = true;
        portEXIT_CRITICAL(&s_mux);
        diag::log("lora: text from %s (%u bytes)", from, (unsigned)len);
        return 0;
    }
};
static TextTap s_tap;
static CallbackObserver<TextTap, const meshtastic_MeshPacket *> s_textObserver(&s_tap, &TextTap::onText);

static void task(void *)
{
    concurrency::hasBeenSetup = true;
    consoleInit();
    console->setDestination(&s_log); // core log -> RAM ring (/meshlog) instead of USB serial
    powerHAL_init();
    powerMonInit();
    initSPI(); // creates spiLock (the core locks it around flash and radio access)
    concurrency::OSThread::setup();
    fsInit();
    struct timeval tv = {time(nullptr), 0}; // the watch keeps UTC from RTC/NTP
    perhapsSetRTC(RTCQualityDevice, &tv);
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    SPI.setFrequency(4000000);
    nodeDB = new NodeDB;
    router = new ReliableRouter();
    service = new MeshService();
    service->init();
    setupModules();
    auto rIf = initLoRa(); // after service->init(): the radio takes its config from the NodeDB
    bool radioOk = (bool)rIf;
    s_radioOk = radioOk;
    airTime = new AirTime();
    if (rIf) router->addInterface(std::move(rIf));
    nodeDB->notifyObservers(true);
    if (textMessageModule) s_textObserver.observe(textMessageModule);
    s_running = true;
    diag::log("lora: core up, radio=%d nodes=%u", radioOk, (unsigned)nodeDB->getNumMeshNodes());
    uint32_t upSince = millis();
    bool stable = false;
    for (;;) {
        if (!stable && millis() - upSince > 60000) { // survived a minute: clear the crash guard
            stable = true;
            Preferences p;
            p.begin("lora", false);
            p.putUChar("boots", 0);
            p.end();
        }
        // the per-loop work of Meshtastic's main loop() that the mesh needs
        Time::serviceMonotonic();
        if (rebootAtMsec && (int32_t)(millis() - rebootAtMsec) >= 0) { // e.g. the app changed the LoRa config
            diag::log("lora: core asked for a reboot");
            delay(100);
            ESP.restart();
        }
        nodeDB->saveNodeDatabaseIfDirty();
        if (RadioLibInterface::instance) {
            static uint32_t lastIrqPoll, lastAgc;
            if (!Throttle::isWithinTimespanMs(lastIrqPoll, 1000)) { // also catches IRQs that fired during light sleep
                lastIrqPoll = millis();
                RadioLibInterface::instance->pollMissedIrqs();
            }
            if (!Throttle::isWithinTimespanMs(lastAgc, AGC_RESET_INTERVAL_MS)) {
                lastAgc = millis();
                RadioLibInterface::instance->updateNoiseFloor();
                RadioLibInterface::instance->periodicRadioMaintenance();
            }
        }
        service->loop();
        long ms = concurrency::mainController.runOrDelay();
        vTaskDelay(pdMS_TO_TICKS(ms < 1 ? 1 : ms > 100 ? 100 : ms));
    }
}

void begin()
{
    if (s_task) return;
    // Crash guard: the setting lives in NVS and the image is already validated, so a core that keeps crashing
    // at boot would loop forever. Three starts without a stable minute switch LoRa off.
    Preferences p;
    p.begin("lora", false);
    uint8_t boots = p.getUChar("boots", 0) + 1;
    p.putUChar("boots", boots >= 3 ? 0 : boots);
    p.end();
    if (boots >= 3) {
        settings::get().lora = false;
        settings::save();
        diag::log("lora: core failed to stay up 3 times, LoRa switched off");
        return;
    }
    xTaskCreatePinnedToCore(task, "mesh", 16384, nullptr, 1, &s_task, 1);
}

bool running() { return s_running; }
uint32_t nodeCount() { return s_running ? nodeDB->getNumMeshNodes() : 0; }
uint32_t messages() { return s_messages; }

bool lastMessage(char *from, size_t fromLen, char *text, size_t textLen)
{
    if (!s_messages) return false;
    portENTER_CRITICAL(&s_mux);
    snprintf(from, fromLen, "%s", s_from);
    snprintf(text, textLen, "%s", s_text);
    portEXIT_CRITICAL(&s_mux);
    return true;
}

bool takeNewMessage()
{
    if (!s_new) return false;
    s_new = false;
    return true;
}

void dumpLog(Print &out) { s_log.dump(out); }

const char *status()
{
    if (!s_task) return "выкл";
    if (!s_running) return "запуск…";
    return s_radioOk ? "в эфире" : "нет радио";
}

} // namespace lora
