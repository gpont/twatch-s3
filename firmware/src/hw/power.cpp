#include "power.h"
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <stdio.h>
#include <esp_timer.h>
#include "battery_estimator.h"
#include "app/settings.h"
#include "clock.h"
#include "haptics.h"
#include <Wire.h>
#include "motion.h"
#include "ota.h"
#include "diag.h"
#include "notify.h"
#include "app/modes.h"

namespace power {

static bool s_on = true;
static Battery s_bat;
static BatteryEstimator s_est;
static bool s_lowWarned = false;
static uint32_t s_ignoreKeyUntil = 0;
static uint32_t s_lastBatMs = 0;
static uint32_t s_lastEstSec = 0;
static uint32_t s_lastLogSec = 0;
static void (*s_background)() = nullptr;
static uint32_t s_sleepTestUntil = 0;   // allow light sleep on the charger (debug)
static bool s_panelSleep = true;        // informational: LilyGoDispSPI::setBrightness(0) already sends SLPIN
static uint32_t s_awakeSince = 0;

// ---- power manager ----
// PM is always configured (80..240 MHz); auto light sleep only while notifications are on.
// Screen on (or Wi-Fi work) holds NO_LIGHT_SLEEP; 240 MHz comes from a CPU_FREQ_MAX lock.
static esp_pm_lock_handle_t s_lockAwake = nullptr, s_lockCpu = nullptr;
static bool s_auto = false, s_awakeHeld = false, s_cpuHeld = false, s_armed = false;

static void holdAwake(bool on)
{
    if (on == s_awakeHeld || !s_lockAwake) return;
    if (on) esp_pm_lock_acquire(s_lockAwake); else esp_pm_lock_release(s_lockAwake);
    s_awakeHeld = on;
}

void setFastCpu(bool on)
{
    if (on == s_cpuHeld || !s_lockCpu) return;
    if (on) esp_pm_lock_acquire(s_lockCpu); else esp_pm_lock_release(s_lockCpu);
    s_cpuHeld = on;
}

static void applyPm()
{
    esp_pm_config_t c = {.max_freq_mhz = 240, .min_freq_mhz = 80, .light_sleep_enable = s_auto};
    esp_err_t e = esp_pm_configure(&c);
    diag::log("pm: auto light sleep=%d (%s)", s_auto, esp_err_to_name(e));
}

static int pmWrite(void *cookie, const char *buf, int n)
{
    ((Print *)cookie)->write((const uint8_t *)buf, n);
    return n;
}

void pmReport(Print &out)
{
    FILE *f = funopen(&out, nullptr, pmWrite, nullptr, nullptr); // newlib here has no open_memstream
    if (!f) return;
    esp_pm_dump_locks(f);
    fclose(f);
}

static constexpr uint32_t kBatReadMs = 30 * 1000;
static constexpr uint32_t kEstSampleSec = 10 * 60;
static constexpr uint32_t kLogEverySec = 3600;
static constexpr uint64_t kTimerWakeUs = 60ULL * 1000 * 1000, kNightTimerWakeUs = 600ULL * 1000 * 1000;
static bool s_nightSave = false;
static uint8_t backlight() { return s_nightSave ? settings::kBrightnessLevels[0] : settings::backlight(); }
static uint32_t timeoutMs() { return (s_nightSave && settings::get().timeoutSec > 5 ? 5 : settings::get().timeoutSec) * 1000UL; }
static constexpr uint8_t kLowPercent = 15;
static constexpr uint8_t kLowRearmPercent = 20;

// ---- hourly battery log (NVS ring, survives reboot) ----
struct LogEntry { uint32_t epoch; uint16_t mv; uint8_t pct; uint8_t charging; };
static constexpr int kLogSize = 48;

static uint32_t uptimeSec() { return (uint32_t)(esp_timer_get_time() / 1000000ULL); }

static void appendLog()
{
    Preferences p;
    p.begin("batlog", false);
    LogEntry ring[kLogSize] = {};
    p.getBytes("ring", ring, sizeof(ring));
    uint8_t head = p.getUChar("head", 0);
    ring[head] = {(uint32_t)clock_svc::now(), s_bat.millivolts, s_bat.percent, (uint8_t)s_bat.charging};
    p.putBytes("ring", ring, sizeof(ring));
    p.putUChar("head", (head + 1) % kLogSize);
    p.end();
    Serial.printf("[bat] %u%% %umV charging=%d left=%ldmin\n", s_bat.percent, s_bat.millivolts, s_bat.charging, (long)s_bat.minutesLeft);
}

void dumpLog(Print &out)
{
    Preferences p;
    p.begin("batlog", true);
    LogEntry ring[kLogSize] = {};
    p.getBytes("ring", ring, sizeof(ring));
    uint8_t head = p.getUChar("head", 0);
    p.end();
    out.println("[batlog] epoch,local,percent,mV,charging,drop_per_hour");
    int prevPct = -1;
    for (int i = 0; i < kLogSize; i++) {
        const LogEntry &e = ring[(head + i) % kLogSize];
        if (!e.epoch) continue;
        time_t t = e.epoch;
        struct tm lt;
        localtime_r(&t, &lt);
        int drop = (prevPct >= 0 && !e.charging) ? prevPct - e.pct : 0;
        out.printf("[batlog] %lu,%02d.%02d %02d:%02d,%u,%u,%u,%d\n", (unsigned long)e.epoch, lt.tm_mday, lt.tm_mon + 1,
                      lt.tm_hour, lt.tm_min, e.pct, e.mv, e.charging, drop);
        prevPct = e.pct;
    }
}

// ---- hourly power stats (what ran and how much the CPU slept), NVS ring like batlog ----
struct HourStat {
    uint32_t epoch;
    uint16_t mv;
    uint8_t pct;
    uint8_t flags;          // 1 charging, 2 auto sleep (BLE on), 4 night save, 8 iPhone link up at the end
    uint16_t screenSec;     // backlight on
    uint16_t wakes;         // screen turned on
    uint16_t sleepPm;       // per mille of the hour in PM auto light sleep
    uint16_t sleepManual;   // per mille in manual esp_light_sleep_start
    uint16_t intervalX100us;// BLE connection interval at the end of the hour
    uint8_t latency;
    uint8_t drops;          // BLE disconnects in the hour
    uint16_t notifs;        // notifications fetched in the hour
};
static constexpr int kStatSize = 48;
static uint64_t s_manualSleepUs = 0, s_screenUs = 0, s_screenSinceUs = 0;
static uint64_t s_hourStartUs = 0, s_pmSleepAtStart = 0, s_manualAtStart = 0, s_screenAtStart = 0;
static uint32_t s_wakesAtStart = 0, s_dropsAtStart = 0, s_notifsAtStart = 0;

// PM "SLEEP" row of esp_pm_dump_locks (needs CONFIG_PM_PROFILING): total us spent in auto light sleep
class BufPrint : public Print {
public:
    char buf[1600];
    size_t n = 0;
    size_t write(uint8_t c) override { if (n < sizeof(buf) - 1) buf[n++] = c; buf[n] = 0; return 1; }
};
static uint64_t pmSleepUs()
{
    static BufPrint p;
    p.n = 0;
    p.buf[0] = 0;
    pmReport(p);
    const char *row = strstr(p.buf, "\nSLEEP");
    if (!row) return 0;
    unsigned long long us = 0;
    // "SLEEP     80 M        12345       12 %" (frequency printed as "80 M")
    if (sscanf(row + 1, "SLEEP %*u M %llu", &us) != 1) sscanf(row + 1, "SLEEP %*s %llu", &us);
    return us;
}

static uint64_t screenUsNow() { return s_screenUs + (s_on ? esp_timer_get_time() - s_screenSinceUs : 0); }

static void statsStart()
{
    s_hourStartUs = esp_timer_get_time();
    s_pmSleepAtStart = pmSleepUs();
    s_manualAtStart = s_manualSleepUs;
    s_screenAtStart = screenUsNow();
    s_wakesAtStart = diag::counters().screenOns;
    s_dropsAtStart = notify::stats().drops;
    s_notifsAtStart = notify::stats().received;
}

static void statsHour()
{
    uint64_t now = esp_timer_get_time(), span = now - s_hourStartUs;
    if (!span) return;
    const notify::Stats &ns = notify::stats();
    HourStat h = {};
    h.epoch = (uint32_t)clock_svc::now();
    h.mv = s_bat.millivolts;
    h.pct = s_bat.percent;
    h.flags = (s_bat.charging ? 1 : 0) | (s_auto ? 2 : 0) | (modes::nightSave() ? 4 : 0) |
              (notify::link() == notify::Link::kReady ? 8 : 0);
    h.screenSec = (uint16_t)((screenUsNow() - s_screenAtStart) / 1000000);
    h.wakes = (uint16_t)(diag::counters().screenOns - s_wakesAtStart);
    h.sleepPm = (uint16_t)((pmSleepUs() - s_pmSleepAtStart) * 1000 / span);
    h.sleepManual = (uint16_t)((s_manualSleepUs - s_manualAtStart) * 1000 / span);
    h.intervalX100us = ns.intervalX100us;
    h.latency = ns.latency;
    h.drops = (uint8_t)(ns.drops - s_dropsAtStart);
    h.notifs = (uint16_t)(ns.received - s_notifsAtStart);
    Preferences p;
    p.begin("pwrstat", false);
    HourStat ring[kStatSize] = {};
    p.getBytes("ring", ring, sizeof(ring));
    uint8_t head = p.getUChar("head", 0);
    ring[head] = h;
    p.putBytes("ring", ring, sizeof(ring));
    p.putUChar("head", (head + 1) % kStatSize);
    p.end();
    diag::log("hour: %s%s bat=%u%%/%umV screen=%us wakes=%u sleep pm=%u.%u%% manual=%u.%u%% ble=%s %u.%ums/lat%u drops=%u notifs=%u",
              s_auto ? "auto" : "manual", modes::nightSave() ? "+night" : "", h.pct, h.mv, h.screenSec, h.wakes, h.sleepPm / 10,
              h.sleepPm % 10, h.sleepManual / 10, h.sleepManual % 10, notify::linkText(), h.intervalX100us / 10,
              h.intervalX100us % 10, h.latency, h.drops, h.notifs);
    statsStart();
}

void dumpStats(Print &out)
{
    Preferences p;
    p.begin("pwrstat", true);
    HourStat ring[kStatSize] = {};
    p.getBytes("ring", ring, sizeof(ring));
    uint8_t head = p.getUChar("head", 0);
    p.end();
    out.println("time,bat%,mV,charging,ble_on,night,link,screen_s,wakes,sleep_pm%,sleep_manual%,interval_ms,latency,drops,notifs");
    for (int i = 0; i < kStatSize; i++) {
        const HourStat &h = ring[(head + i) % kStatSize];
        if (!h.epoch) continue;
        time_t t = h.epoch;
        struct tm lt;
        localtime_r(&t, &lt);
        out.printf("%02d.%02d %02d:%02d,%u,%u,%u,%u,%u,%u,%u,%u,%u.%u,%u.%u,%u.%u,%u,%u,%u\n", lt.tm_mday, lt.tm_mon + 1, lt.tm_hour,
                   lt.tm_min, h.pct, h.mv, h.flags & 1, (h.flags >> 1) & 1, (h.flags >> 2) & 1, (h.flags >> 3) & 1, h.screenSec,
                   h.wakes, h.sleepPm / 10, h.sleepPm % 10, h.sleepManual / 10, h.sleepManual % 10, h.intervalX100us / 10,
                   h.intervalX100us % 10, h.latency, h.drops, h.notifs);
    }
}

// ---- battery ----
static void readBattery()
{
    s_bat.percent = (uint8_t)instance.getBatteryPercent();
    s_bat.millivolts = (uint16_t)(instance.getBattVoltage());
    s_bat.charging = instance.isCharging();
    s_bat.usb = instance.pmic.isVbusGood();

    uint32_t up = uptimeSec();
    if (s_bat.charging || s_lastEstSec == 0 || up - s_lastEstSec >= kEstSampleSec) {
        s_est.addSample(up, s_bat.percent, s_bat.charging);
        s_lastEstSec = up;
    }
    s_bat.minutesLeft = s_est.minutesLeft();

    s_bat.low = !s_bat.charging && s_bat.percent <= kLowPercent;
    if (s_bat.low && !s_lowWarned) {
        s_lowWarned = true;
        haptics::lowBattery();
    }
    if (s_bat.charging || s_bat.percent >= kLowRearmPercent) s_lowWarned = false;

    if (up - s_lastLogSec >= kLogEverySec) {
        s_lastLogSec = up;
        appendLog();
    }
}

const Battery &battery() { return s_bat; }

// ---- screen ----
static void disarm();

void screenOnNow()
{
    disarm();
    holdAwake(true);
    if (s_on) { userActivity(); return; }
    instance.setBrightness(backlight());
    s_on = true;
    s_screenSinceUs = esp_timer_get_time();
    diag::countScreenOn();
    lv_obj_invalidate(lv_screen_active());
    userActivity();
}

void screenOffNow()
{
    if (!s_on) return;
    diag::stage(diag::Stage::ScreenOff);
    diag::log("screen off");
    instance.setBrightness(0);
    s_on = false;
    s_screenUs += esp_timer_get_time() - s_screenSinceUs;
    motion::persistSteps();
}

bool screenOn() { return s_on; }

void userActivity() { lv_display_trigger_activity(nullptr); }

static void onDeviceEvent(const DeviceEvent &e, void *)
{
    if (e.type == POWER_EVENT && e.payload.pmu == PMU_EVENT_KEY_CLICKED) {
        if (millis() < s_ignoreKeyUntil) return; // this click already woke us
        diag::log("event: crown click (screen %s)", s_on ? "on->off" : "off->on");
        if (s_on) screenOffNow(); else screenOnNow();
    } else if (e.type == SENSOR_EVENT &&
               (e.payload.sensor == SENSOR_TILT_DETECTED || e.payload.sensor == SENSOR_DOUBLE_TAP_DETECTED)) {
        if (!motion::wakeGestureEnabled()) return;
        if (!s_on) diag::log("event: wake gesture");
        screenOnNow();
    } else if (e.type == POWER_EVENT &&
               (e.payload.pmu == PMU_EVENT_USBC_INSERT || e.payload.pmu == PMU_EVENT_USBC_REMOVE)) {
        readBattery();
        if (e.payload.pmu == PMU_EVENT_USBC_INSERT) dumpLog();
    }
}

// ---- light sleep ----
// Wake lines go through the RTC controller, like LilyGoLib's own lightSleep(): ext1 "any low" for TP_INT and
// PMU_INT, ext0 "high" for SENSOR_INT (accelerometer INT1 is active-high push-pull; the board pulls it low). gpio_wakeup_enable() was tried
// first and never fired on this board, even with gpio_sleep_sel_dis(); it also left the pins as LEVEL
// interrupts, which starved the CPU after a wake (the frozen-screen hang).
static constexpr uint64_t pinBit(int pin) { return 1ULL << pin; }

// LilyGoLib attached edge ISRs; make sure nothing left them level-triggered.
static void restoreEdgeInterrupts()
{
    const struct { int pin; gpio_int_type_t edge; } pins[] = {
        {TP_INT, GPIO_INTR_NEGEDGE}, {PMU_INT, GPIO_INTR_NEGEDGE}, {SENSOR_INT, GPIO_INTR_POSEDGE}};
    for (auto &p : pins) {
        gpio_set_intr_type((gpio_num_t)p.pin, p.edge);
        gpio_intr_enable((gpio_num_t)p.pin);
    }
}

static uint64_t s_lastWakePins = 0;
static esp_sleep_wakeup_cause_t s_lastCause = ESP_SLEEP_WAKEUP_UNDEFINED;
static uint8_t s_selfTest = 0; // 0 = off, 1.. = stage

static void sleepOnce(uint64_t extraMask = 0)
{
    diag::stage(diag::Stage::PreSleep);
    motion::clearInterrupt();
    instance.pmic.getIrq()->clearStatus();

    uint64_t mask = pinBit(PMU_INT) | extraMask;
    if (settings::get().tapWake) mask |= pinBit(TP_INT);
    bool gestureWake = motion::wakeGestureEnabled();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_disable_ext1_wakeup_io(0); // IDF 5 appends pins: start from a clean mask
    esp_err_t err = esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);
    if (err != ESP_OK) diag::log("ext1 setup failed: %s", esp_err_to_name(err));
    rtc_gpio_pullup_en((gpio_num_t)PMU_INT);
    rtc_gpio_pullup_en((gpio_num_t)TP_INT);
    if (gestureWake) esp_sleep_enable_ext0_wakeup((gpio_num_t)SENSOR_INT, 1);
    if (extraMask & pinBit(RTC_INT)) rtc_gpio_pullup_en((gpio_num_t)RTC_INT);
    esp_sleep_enable_timer_wakeup(s_nightSave ? kNightTimerWakeUs : kTimerWakeUs);

    // a line already low here means an instant wake loop
    int tp = gpio_get_level((gpio_num_t)TP_INT), pmu = gpio_get_level((gpio_num_t)PMU_INT), sen = gpio_get_level((gpio_num_t)SENSOR_INT);
    if (!tp || !pmu || (sen && gestureWake)) diag::log("pre-sleep lines active: tp=%d pmu=%d sensor=%d", tp, pmu, sen);

    uint32_t awake = millis() - s_awakeSince;
    if (awake > diag::counters().longestAwakeMs) const_cast<diag::Counters &>(diag::counters()).longestAwakeMs = awake;
    diag::countSleep();
    diag::stage(diag::Stage::Sleeping);
    Serial.flush();
    disableLoopWDT();      // the loop task is legitimately blocked here for up to 60 s
    diag::watchdog(s_nightSave ? 615000 : 75000); // ...but not longer: the timer must wake us
    int64_t sleptFrom = esp_timer_get_time();
    esp_light_sleep_start();
    s_manualSleepUs += esp_timer_get_time() - sleptFrom;
    diag::watchdog(10000);
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    uint64_t pins = cause == ESP_SLEEP_WAKEUP_EXT1 ? esp_sleep_get_ext1_wakeup_status() : 0;
    s_lastWakePins = pins;
    s_lastCause = cause;
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
    esp_sleep_disable_ext1_wakeup_io(0);
    rtc_gpio_deinit((gpio_num_t)SENSOR_INT); // ext0 moved the pad to the RTC mux: give it back to the digital GPIO/ISR
    restoreEdgeInterrupts();
    enableLoopWDT();
    s_awakeSince = millis();

    if (cause == ESP_SLEEP_WAKEUP_EXT1 || cause == ESP_SLEEP_WAKEUP_EXT0) {
        bool gesture = cause == ESP_SLEEP_WAKEUP_EXT0, crown = pins & pinBit(PMU_INT), touch = pins & pinBit(TP_INT);
        diag::Wake w = gesture ? diag::Wake::Gesture : crown ? diag::Wake::Crown : touch ? diag::Wake::Touch : diag::Wake::GpioUnknown;
        diag::countWake(w);
        diag::log("wake: %s (%s pins=0x%llx)", diag::wakeName(w), cause == ESP_SLEEP_WAKEUP_EXT0 ? "ext0" : "ext1", (unsigned long long)pins);
        s_ignoreKeyUntil = millis() + 800;
        diag::stage(diag::Stage::WakeBoardLoop);
        instance.loop(); // dispatch the PMU / sensor IRQ that woke us
        diag::stage(diag::Stage::WakeClearSensor);
        if (gesture) motion::clearInterrupt();
        diag::stage(diag::Stage::WakeScreenOn);
        if (!s_selfTest) screenOnNow();
    } else {
        diag::countWake(cause == ESP_SLEEP_WAKEUP_TIMER ? diag::Wake::Timer : diag::Wake::Other);
        if (cause != ESP_SLEEP_WAKEUP_TIMER) diag::log("wake: cause=%d", (int)cause);
        diag::stage(diag::Stage::WakeBoardLoop);
        instance.loop();
    }
}

// ---- auto light sleep (notifications on) ----
// The CPU sleeps on its own between ticks; ext0/ext1 make the wake lines end a sleep, and the loop
// checks them every 100 ms. Touch goes to polling mode while armed so TP_INT stays low for the whole
// touch (in trigger mode it only pulses and a 100 ms poll would miss it).
static void touchIntPolling(bool polling)
{
    Wire1.beginTransmission(0x38);
    Wire1.write(0xA4); // G_MODE: 0 = INT low while touched, 1 = pulse per report
    Wire1.write(polling ? 0 : 1);
    int r = Wire1.endTransmission();
    if (r) diag::log("touch G_MODE=%d write failed: %d", !polling, r);
}

static void arm()
{
    if (s_armed) return;
    motion::clearInterrupt();
    instance.pmic.getIrq()->clearStatus();
    touchIntPolling(true);
    uint64_t mask = pinBit(PMU_INT);
    if (settings::get().tapWake) mask |= pinBit(TP_INT);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
    esp_sleep_disable_ext1_wakeup_io(0);
    esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);
    rtc_gpio_pullup_en((gpio_num_t)PMU_INT);
    rtc_gpio_pullup_en((gpio_num_t)TP_INT);
    if (motion::wakeGestureEnabled()) esp_sleep_enable_ext0_wakeup((gpio_num_t)SENSOR_INT, 1);
    s_armed = true;
}

static void disarm()
{
    if (!s_armed) return;
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
    esp_sleep_disable_ext1_wakeup_io(0);
    rtc_gpio_deinit((gpio_num_t)SENSOR_INT); // ext0 moved the pad to the RTC mux
    restoreEdgeInterrupts();
    touchIntPolling(false);
    s_armed = false;
}

static void autoWake(diag::Wake w)
{
    diag::countWake(w);
    diag::log("wake: %s (auto sleep)", diag::wakeName(w));
    s_ignoreKeyUntil = millis() + 800; // LilyGoLib may still deliver the same crown click
    screenOnNow();
}

static void autoIdle()
{
    arm();
    holdAwake(false);
    if (!gpio_get_level((gpio_num_t)PMU_INT)) {
        uint64_t st = instance.pmic.irq().readStatus();
        bool key = instance.pmic.irq().isPekeyShortPress(st);
        bool vbus = instance.pmic.irq().isVbusInsert(st) || instance.pmic.irq().isVbusRemove(st);
        instance.pmic.getIrq()->clearStatus();
        if (vbus) readBattery();
        if (key) { autoWake(diag::Wake::Crown); return; }
    }
    if (settings::get().tapWake && !gpio_get_level((gpio_num_t)TP_INT)) { autoWake(diag::Wake::Touch); return; }
    if (motion::wakeGestureEnabled() &&
        (gpio_get_level((gpio_num_t)SENSOR_INT) || rtc_gpio_get_level((gpio_num_t)SENSOR_INT))) {
        motion::clearInterrupt();
        autoWake(diag::Wake::Gesture);
        return;
    }
    static uint32_t lastBg = 0;
    if (s_background && millis() - lastBg >= 60000) { lastBg = millis(); s_background(); }
    diag::feed();
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)); // BLE worker notifies on news: react at once
}

void setAutoSleep(bool on)
{
    if (on == s_auto) return;
    disarm();
    holdAwake(true);
    s_auto = on;
    applyPm();
}

bool autoSleep() { return s_auto; }

void setNightSave(bool on)
{
    s_nightSave = on;
    if (s_on) instance.setBrightness(backlight());
}

// ---- wake self-test (no human needed) ----
// Stage 1: PCF8563 countdown pulls RTC_INT low after 8 s  -> proves ext1 wakes the chip at all.
// Stage 2: DRV2605 plays "wait ~3.8 s, buzz" on its own while we sleep; any-motion on the accelerometer
//          sees the vibration and pulls SENSOR_INT low -> proves the accelerometer wake line end to end.
static constexpr uint8_t kRtcAddr = 0x51;
static void rtcWrite(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(kRtcAddr);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}
static void rtcCountdown(uint8_t seconds)
{
    rtcWrite(0x0F, seconds); // timer value
    rtcWrite(0x0E, 0x82);    // TE=1, 1 Hz source (SensorLib's setCountdownTimer drops TE)
    rtcWrite(0x01, 0x01);    // TIE=1: INT low while TF is set; clears TF/AF
}
static void rtcCountdownOff()
{
    rtcWrite(0x01, 0x00);
    rtcWrite(0x0E, 0x03); // TE=0, lowest-power source
}

static void selfTestStep()
{
    static uint32_t t0;
    switch (s_selfTest) {
    case 1:
        diag::log("selftest 1/2: RTC countdown 8 s");
        rtcCountdown(8);
        t0 = millis();
        sleepOnce(pinBit(RTC_INT));
        diag::log("selftest 1/2: %s after %lums (cause=%d pins=0x%llx)",
                  (s_lastWakePins & pinBit(RTC_INT)) ? "PASS" : "FAIL", (unsigned long)(millis() - t0), (int)s_lastCause,
                  (unsigned long long)s_lastWakePins);
        rtcCountdownOff();
        s_selfTest = 2;
        break;
    case 2: {
        diag::log("selftest 2/2: haptic buzz -> any-motion -> SENSOR_INT");
        motion::setTestMotionIrq(true);
        static const uint8_t p[] = {haptics::wait10ms(127), haptics::wait10ms(127), haptics::wait10ms(127), 47, 47, 47, 47};
        haptics::play(p, sizeof(p));
        t0 = millis();
        bool gestureWas = motion::wakeGestureEnabled();
        motion::setWakeGestureEnabled(true); // include SENSOR_INT in the mask
        sleepOnce();
        diag::log("selftest 2/2: %s after %lums (cause=%d pins=0x%llx)",
                  s_lastCause == ESP_SLEEP_WAKEUP_EXT0 ? "PASS" : "FAIL", (unsigned long)(millis() - t0), (int)s_lastCause,
                  (unsigned long long)s_lastWakePins);
        haptics::stop();
        motion::setTestMotionIrq(false);
        motion::setWakeGestureEnabled(gestureWas);
        s_selfTest = 0;
        diag::log("selftest done");
        s_sleepTestUntil = 0; // Wi-Fi back
        break;
    }
    }
}

void requestSelfTest()
{
    s_selfTest = 1;
    s_sleepTestUntil = millis() + 180000;
    diag::log("selftest requested");
}

void setSleepTest(uint32_t seconds)
{
    s_sleepTestUntil = seconds ? millis() + seconds * 1000 : 0;
    diag::log("sleeptest %lus", (unsigned long)seconds);
}
bool sleepTestActive() { return s_sleepTestUntil && (int32_t)(s_sleepTestUntil - millis()) > 0; }
void setPanelSleep(bool on) { s_panelSleep = on; diag::log("panel sleep %d", on); }
bool panelSleep() { return s_panelSleep; }

void setBackgroundWork(void (*fn)()) { s_background = fn; }

void begin()
{
    instance.onEvent(onDeviceEvent);
    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "screen", &s_lockAwake);
    esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "fastcpu", &s_lockCpu);
    holdAwake(true);
    s_auto = settings::get().notifications;
    applyPm();
    instance.setBrightness(settings::backlight());
    s_lastLogSec = uptimeSec();
    readBattery();
    dumpLog();
    s_screenSinceUs = esp_timer_get_time(); // the screen starts on
    statsStart();
}

void tick()
{
    uint32_t ms = millis();
    if (esp_timer_get_time() - s_hourStartUs >= 3600ULL * 1000000) statsHour();
    if (ms - s_lastBatMs > kBatReadMs) {
        s_lastBatMs = ms;
        readBattery();
    }

    if (s_on) {
        if (lv_display_get_inactive_time(nullptr) > timeoutMs()) screenOffNow();
        return;
    }
    if (clock_svc::busy() || (ota::keepAwake() && !sleepTestActive())) {
        disarm();
        holdAwake(true);
        // awake with the screen off (charger / Wi-Fi job): nothing else watches the touch panel here
        if (settings::get().tapWake && gpio_get_level((gpio_num_t)TP_INT) == 0) {
            diag::log("event: tap while awake");
            screenOnNow();
            return;
        } // Wi-Fi job / OTA mode: stay awake with the screen off, but yield
        static uint32_t lastBg = 0;
        if (s_background && ms - lastBg > 1000) { lastBg = ms; s_background(); }
        diag::feed();
        vTaskDelay(pdMS_TO_TICKS(20));
        return;
    }
    if (s_auto) { autoIdle(); return; }
    if (s_selfTest) { selfTestStep(); return; }
    sleepOnce();
    diag::stage(diag::Stage::Background);
    if (!s_on && s_background) s_background();
}

} // namespace power
