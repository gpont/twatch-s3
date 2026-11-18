#include "ota.h"
#include <LilyGoLib.h>
#include <Network.h> // explicit includes: PlatformIO's LDF misses ArduinoOTA's dependencies
#include <ESPmDNS.h>
#include <Update.h>
#include <WiFi.h>
#include <FS.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_core_dump.h>
#include <time.h>
#include "clock.h"
#include "build_info.h"
#include "diag.h"
#include "motion.h"
#include "power.h"
#include "app/settings.h"
#include <StreamString.h>
#include <LV_Helper.h>
#include "ui/ui.h"
#include "notify.h"
#include "app/modes.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif
#ifndef FW_BUILD_TIME
#define FW_BUILD_TIME "0"
#endif

namespace ota {

static constexpr uint8_t kMaxTries = 3;
static constexpr uint32_t kValidateMs = 30 * 1000;
static constexpr uint32_t kReconnectMs = 30 * 1000;

static bool s_pending = false;     // fresh image not yet validated
static uint32_t s_validateAt = 0;
static bool s_active = false;      // OTA network mode on
static bool s_otaStarted = false;
static uint32_t s_lastConnectTry = 0;
static WebServer s_http(80);
static uint32_t s_graceUntil = 0;
static bool s_suspended = false;   // after an OTA done on battery: stay online a while so the uploader can reach us
static constexpr uint32_t kGraceMs = 10UL * 60 * 1000;
static bool s_authOk = false;
static char s_status[48] = "выкл";
static char s_version[20] = "";

const char *version()
{
    if (!s_version[0]) {
        time_t t = (time_t)strtol(FW_BUILD_TIME, nullptr, 10);
        struct tm lt;
        localtime_r(&t, &lt);
        snprintf(s_version, sizeof(s_version), "%02d.%02d %02d:%02d", lt.tm_mday, lt.tm_mon + 1, lt.tm_hour, lt.tm_min);
    }
    return s_version;
}

void guardEarly()
{
    Preferences p;
    p.begin("ota", false);
    s_pending = p.getBool("pending", false);
    if (s_pending) {
        uint8_t tries = p.getUChar("tries", 0) + 1;
        p.putUChar("tries", tries);
        String prev = p.getString("prev", "");
        diag::log("ota: unvalidated image, boot try %u/%u", tries, kMaxTries);
        if (tries > kMaxTries) {
            const esp_partition_t *back = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, prev.c_str());
            p.putBool("pending", false);
            p.putUChar("tries", 0);
            p.end();
            if (back && esp_ota_set_boot_partition(back) == ESP_OK) {
                diag::log("ota: ROLLBACK to %s", prev.c_str());
                Serial.flush();
                esp_restart();
            }
            diag::log("ota: rollback target missing, keeping this image");
            s_pending = false;
            return;
        }
        s_validateAt = millis() + kValidateMs;
    }
    if (p.getBool("grace", false)) {
        p.putBool("grace", false);
        s_graceUntil = millis() + kGraceMs;
        Serial.println("[ota] uploaded on battery: staying online for 10 min");
    }
    p.end();
}

static void markValid()
{
    Preferences p;
    p.begin("ota", false);
    p.putBool("pending", false);
    p.putUChar("tries", 0);
    p.end();
    s_pending = false;
    diag::log("ota: image %s validated", esp_ota_get_running_partition()->label);
}

static void onOtaEnd()
{
    // the image just written becomes "pending"; remember where to come back to
    Preferences p;
    p.begin("ota", false);
    p.putBool("pending", true);
    p.putUChar("tries", 0);
    p.putString("prev", esp_ota_get_running_partition()->label);
    p.putBool("grace", !instance.pmic.isVbusGood()); // uploaded on battery (forced network): keep Wi-Fi after reboot
    p.end();
    motion::persistSteps(true); // don't lose up to 5 min of steps to the reboot
    diag::log("ota: upload done, rebooting into the new image");
}

// HTTP upload instead of ArduinoOTA: one TCP connection from the uploader (macOS lets /usr/bin/curl
// reach the LAN, unlike espota's UDP + reverse connection). Upload:
//   curl -u ota:<OTA_PASSWORD> -F firmware=@firmware.bin http://twatch.local/update
static void handleUpload()
{
    HTTPUpload &up = s_http.upload();
    if (up.status == UPLOAD_FILE_START) {
        s_authOk = !strlen(OTA_PASSWORD) || s_http.authenticate("ota", OTA_PASSWORD);
        if (!s_authOk) return;
        snprintf(s_status, sizeof(s_status), "заливка...");
        diag::log("ota: upload start: %s", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
    } else if (!s_authOk) {
        return;
    } else if (up.status == UPLOAD_FILE_WRITE) {
        feedLoopWDT(); // the whole upload runs inside one handleClient() call
        diag::feed();
        if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) onOtaEnd();
        else Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        diag::log("ota: upload aborted");
    }
}

static void handleUploadDone()
{
    if (!s_authOk) return s_http.requestAuthentication();
    if (Update.hasError() || !Update.isFinished()) {
        s_http.send(500, "text/plain", String("FAIL ") + Update.errorString() + "\n");
        snprintf(s_status, sizeof(s_status), "ошибка заливки");
        return;
    }
    s_http.send(200, "text/plain", "OK, rebooting\n");
    delay(300);
    ESP.restart();
}

static void handleInfo()
{
    char buf[200];
    snprintf(buf, sizeof(buf), "fw=%s slot=%s pending=%d battery=%u%% uptime=%lus\n", version(),
             esp_ota_get_running_partition()->label, s_pending, (unsigned)instance.getBatteryPercent(),
             (unsigned long)(millis() / 1000));
    s_http.send(200, "text/plain", buf);
}

static bool requireAuth()
{
    if (!strlen(OTA_PASSWORD) || s_http.authenticate("ota", OTA_PASSWORD)) return true;
    s_http.requestAuthentication();
    return false;
}

// GET /debug                      counters, pins, steps, battery
// GET /debug?sleeptest=SEC        (auth) light-sleep on the charger for SEC s, Wi-Fi off meanwhile
// GET /debug?panelsleep=0|1       (auth) ST7789 sleep-in when the screen is off
// GET /debug?face=N / tile=N / row=N (auth) switch watch face / screen / scroll settings (for screenshots)
// GET /debug?pm=1               (auth) power manager lock/mode stats
// GET /debug?notif=TEXT[&call=1] (auth) fake notification (UI/haptics test without the phone)
// GET /debug?forget=1            (auth) delete BLE bonds (pair the iPhone again)
// GET /debug?led=0..4            (auth) AXP2101 CHGLED: 0 off(HiZ) 1 low 2 high 3 blink 1 Hz 4 blink 4 Hz
// GET /debug?selftest=1          (auth) wake self-test without a human: RTC timer + haptic -> accelerometer
// GET /debug?wdttest=task|rtc     (auth) hang on purpose to verify the watchdogs (task WDT / RTC WDT only)
static void handleDebug()
{
    if (s_http.hasArg("sleeptest") || s_http.hasArg("panelsleep") || s_http.hasArg("wdttest") || s_http.hasArg("selftest") ||
        s_http.hasArg("face") || s_http.hasArg("tile") || s_http.hasArg("row") || s_http.hasArg("pm") ||
        s_http.hasArg("notif") || s_http.hasArg("forget") || s_http.hasArg("led")) {
        if (!requireAuth()) return;
        if (s_http.hasArg("pm")) {
            StreamString o;
            o.printf("autoSleep=%d ble=%s\n", power::autoSleep(), notify::linkText());
            power::pmReport(o);
            s_http.send(200, "text/plain", (const String &)o);
            return;
        }
        if (s_http.hasArg("notif")) {
            notify::inject(s_http.arg("notif").c_str(), s_http.arg("msg").c_str(), s_http.hasArg("call"));
            s_http.send(200, "text/plain", "ok\n");
            return;
        }
        if (s_http.hasArg("led")) {
            int v = s_http.arg("led").toInt();
            instance.pmic.led().setMode(PmicLedBase::Mode::MANUAL);
            bool ok = instance.pmic.led().setManualState((PmicLedBase::ManualState)(v < 0 || v > 4 ? 0 : v));
            s_http.send(200, "text/plain", ok ? "ok\n" : "failed\n");
            return;
        }
        if (s_http.hasArg("forget")) {
            notify::forgetPhone();
            s_http.send(200, "text/plain", "bonds deleted; forget 'twatch' on the iPhone too, then pair again\n");
            return;
        }
        if (s_http.hasArg("face") || s_http.hasArg("tile") || s_http.hasArg("row")) {
            power::screenOnNow();
            if (s_http.hasArg("face")) ui::setFace(s_http.arg("face").toInt());
            if (s_http.hasArg("tile")) ui::showTile(s_http.arg("tile").toInt());
            if (s_http.hasArg("row")) { ui::showTile(3); ui::scrollToRow(s_http.arg("row").toInt()); }
            s_http.send(200, "text/plain", "ok\n");
            return;
        }
        if (s_http.hasArg("selftest")) {
            s_http.send(200, "text/plain", "wake self-test started: Wi-Fi off ~1-2 min, then GET /log\n");
            power::requestSelfTest();
            return;
        }
        if (s_http.hasArg("wdttest")) { // deliberate hang: the watchdogs must reboot us and /log must name the stage
            s_http.send(200, "text/plain", "hanging now; expect a reboot within ~10 s\n");
            bool rtcOnly = s_http.arg("wdttest") == "rtc"; // rtc: only the RTC watchdog can save us
            diag::log("wdttest(%s): hanging on purpose", rtcOnly ? "rtc" : "task");
            delay(100);
            if (rtcOnly) disableLoopWDT();
            for (;;) {}
        }
        if (s_http.hasArg("panelsleep")) power::setPanelSleep(s_http.arg("panelsleep").toInt() != 0);
        if (s_http.hasArg("sleeptest")) {
            uint32_t sec = s_http.arg("sleeptest").toInt();
            s_http.send(200, "text/plain", "sleeptest armed: Wi-Fi off for " + String(sec) + " s. Then GET /debug and /log.\n");
            power::setSleepTest(sec);
            return;
        }
    }
    const diag::Counters &c = diag::counters();
    const StepsDay::State &st = motion::steps();
    const power::Battery &b = power::battery();
    StreamString o;
    o.printf("fw=%s slot=%s pending=%d uptime=%lus heap=%u\n", version(), esp_ota_get_running_partition()->label, s_pending,
             (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap());
    o.printf("boots=%lu sleeps=%lu screenOns=%lu longestAwake=%lums\n", (unsigned long)c.boots, (unsigned long)c.sleeps,
             (unsigned long)c.screenOns, (unsigned long)c.longestAwakeMs);
    o.print("wakes:");
    for (int i = 0; i < (int)diag::Wake::Count; i++) o.printf(" %s=%lu", diag::wakeName((diag::Wake)i), (unsigned long)c.wakes[i]);
    o.printf("\npins now: tp_int=%d pmu_int=%d sensor_int=%d\n", gpio_get_level((gpio_num_t)TP_INT),
             gpio_get_level((gpio_num_t)PMU_INT), gpio_get_level((gpio_num_t)SENSOR_INT));
    o.printf("accel=%s rawSteps=%lu wakeGesture=%d\n", motion::modelName(), (unsigned long)motion::rawHardwareSteps(), motion::wakeGestureEnabled());
    o.printf("stepsDay: day=%ld today=%lu lastSensor=%lu history=", (long)st.day, (unsigned long)st.today, (unsigned long)st.lastSensor);
    for (int i = 0; i < StepsDay::kHistoryDays; i++) o.printf("%lu%s", (unsigned long)st.history[i], i + 1 < StepsDay::kHistoryDays ? "," : "\n");
    o.printf("battery=%u%% %umV charging=%d usb=%d left=%ldmin\n", b.percent, b.millivolts, b.charging, b.usb, (long)b.minutesLeft);
    o.printf("notifications=%d autoSleep=%d ble=%s items=%d nightSave=%d(active %d)\n", settings::get().notifications,
             power::autoSleep(), notify::linkText(), notify::store().count(), settings::get().nightSave, modes::nightSave());
    o.printf("screenOn=%d panelSleep=%d sleepTest=%d timeout=%us clockValid=%d\n", power::screenOn(), power::panelSleep(),
             power::sleepTestActive(), settings::get().timeoutSec, clock_svc::valid());
    s_http.send(200, "text/plain", (const String &)o);
}

// GET /screenshot -> 240x240 24-bit BMP of what the display shows (LILYGO_SCREEN_CAPTURE_ENABLED).
// Turns the screen on and lets LVGL render first if it was off.
static void handleScreenshot()
{
    if (!power::screenOn()) power::screenOnNow();
    power::userActivity();
    lv_obj_invalidate(lv_screen_active());
    for (int i = 0; i < 20; i++) { lv_timer_handler(); delay(10); }
    lv_screen_capture_t cap;
    if (!lv_get_screen_capture(&cap) || !cap.pixels) { s_http.send(500, "text/plain", "capture unavailable\n"); return; }
    const uint32_t w = cap.width, h = cap.height, row = w * 3, size = 54 + row * h;
    uint8_t hdr[54] = {'B', 'M'};
    auto put32 = [&](int o, uint32_t v) { hdr[o] = v; hdr[o + 1] = v >> 8; hdr[o + 2] = v >> 16; hdr[o + 3] = v >> 24; };
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
    hdr[26] = 1; hdr[28] = 24; put32(34, row * h);
    s_http.setContentLength(size);
    s_http.send(200, "image/bmp", "");
    s_http.sendContent((const char *)hdr, sizeof(hdr));
    static uint8_t line[240 * 3];
    for (int y = h - 1; y >= 0; y--) { // BMP rows go bottom-up
        for (uint32_t x = 0; x < w; x++) {
            uint16_t c = cap.pixels[y * w + x];
            if (cap.rgb565_swapped) c = (c >> 8) | (c << 8);
            line[x * 3 + 0] = (c & 0x1F) << 3;          // B
            line[x * 3 + 1] = ((c >> 5) & 0x3F) << 2;   // G
            line[x * 3 + 2] = ((c >> 11) & 0x1F) << 3;  // R
        }
        s_http.sendContent((const char *)line, row);
        feedLoopWDT();
        diag::feed();
    }
}

// GET /coredump -> summary of the last panic stored in the coredump partition (task, PC, cause,
// backtrace). Resolve addresses with xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/<env>/firmware.elf
static void handleCoredump()
{
    esp_core_dump_summary_t *sum = (esp_core_dump_summary_t *)malloc(sizeof(esp_core_dump_summary_t));
    if (!sum) { s_http.send(500, "text/plain", "oom\n"); return; }
    esp_err_t e = esp_core_dump_get_summary(sum);
    if (e != ESP_OK) { free(sum); s_http.send(200, "text/plain", String("no coredump: ") + esp_err_to_name(e) + "\n"); return; }
    StreamString o;
    o.printf("task=%s pc=0x%08lx exccause=%lu excvaddr=0x%08lx\nbacktrace:", sum->exc_task, (unsigned long)sum->exc_pc,
             (unsigned long)sum->ex_info.exc_cause, (unsigned long)sum->ex_info.exc_vaddr);
    for (int i = 0; i < sum->exc_bt_info.depth; i++) o.printf(" 0x%08lx", (unsigned long)sum->exc_bt_info.bt[i]);
    o.printf("%s\n", sum->exc_bt_info.corrupted ? " (corrupted)" : "");
    free(sum);
    s_http.send(200, "text/plain", (const String &)o);
}

static void handleLog()
{
    static char buf[3200];
    diag::readLog(buf, sizeof(buf));
    s_http.send(200, "text/plain", buf);
}

static void handleBatlog()
{
    StreamString o;
    power::dumpLog(o);
    s_http.send(200, "text/plain", (const String &)o);
}

static void handleStats()
{
    StreamString o;
    power::dumpStats(o);
    s_http.send(200, "text/plain", (const String &)o);
}

static void startOta()
{
    MDNS.begin("twatch");
    MDNS.addService("http", "tcp", 80);
    s_http.on("/info", HTTP_GET, handleInfo);
    s_http.on("/debug", HTTP_GET, handleDebug);
    s_http.on("/log", HTTP_GET, handleLog);
    s_http.on("/coredump", HTTP_GET, handleCoredump);
    s_http.on("/screenshot", HTTP_GET, handleScreenshot);
    s_http.on("/batlog", HTTP_GET, handleBatlog);
    s_http.on("/stats", HTTP_GET, handleStats);
    s_http.on("/update", HTTP_POST, handleUploadDone, handleUpload);
    s_http.begin();
    s_otaStarted = true;
}

static void stopNetwork()
{
    if (s_otaStarted) { s_http.stop(); MDNS.end(); s_otaStarted = false; }
    s_active = false;
    snprintf(s_status, sizeof(s_status), "выкл (нет зарядки)");
    if (!clock_svc::busy()) { WiFi.disconnect(true, false); WiFi.mode(WIFI_OFF); }
}

void tick()
{
    if (s_pending) {
        if (millis() > s_validateAt) markValid();
    }

    if (s_suspended) { // MDNS.end() after Wi-Fi is already gone crashes in esp_netif: tear down in order, here
        if (s_active) stopNetwork();
        return;
    }
    bool grace = s_graceUntil && (int32_t)(s_graceUntil - millis()) > 0;
    if (s_graceUntil && !grace) s_graceUntil = 0;
    bool charger = (instance.pmic.isVbusGood() && !power::sleepTestActive()) || grace;
    if (!charger) {
        if (s_active) stopNetwork();
        return;
    }
    if (!strlen(WIFI_SSID)) { snprintf(s_status, sizeof(s_status), "нет Wi-Fi в secrets.h"); return; }

    s_active = true;
    if (WiFi.status() != WL_CONNECTED) {
        if (s_otaStarted) { s_http.stop(); MDNS.end(); s_otaStarted = false; }
        uint32_t ms = millis();
        if (s_lastConnectTry == 0 || ms - s_lastConnectTry > kReconnectMs) {
            s_lastConnectTry = ms;
            WiFi.mode(WIFI_STA);
            WiFi.begin(WIFI_SSID, WIFI_PASS);
            snprintf(s_status, sizeof(s_status), "подключение к Wi-Fi...");
        }
        return;
    }
    if (!s_otaStarted) {
        startOta();
        clock_svc::requestSync(); // Wi-Fi is up anyway: correct the RTC
        snprintf(s_status, sizeof(s_status), "twatch.local %s", WiFi.localIP().toString().c_str());
        diag::log("ota: listening as twatch.local (%s)", WiFi.localIP().toString().c_str());
    }
    s_http.handleClient();
}

bool keepAwake() { return s_active || s_pending; }
bool networkActive() { return s_active; }
void suspendNetwork(bool on) { s_suspended = on; }
const char *status() { return s_status; }

} // namespace ota
