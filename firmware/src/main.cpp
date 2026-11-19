// T-Watch S3 custom firmware. Architecture and decisions: docs/adr/.
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <esp_task_wdt.h>
#include <Wire.h>
#include "app/modes.h"
#include "app/settings.h"
#include "hw/clock.h"
#include "hw/diag.h"
#include "hw/haptics.h"
#include "hw/motion.h"
#include "hw/notify.h"
#include "hw/ota.h"
#include "hw/power.h"
#include "lora/mesh_core.h"
#include "local_time.h"
#include "night.h"
#include "ui/ui.h"


// ---- power optimizations (docs/power/README.md §6, ADR 0007) ----
// LoRa: radio.begin() leaves the SX1262 in standby (~0.6-0.8 mA); we don't use it in v1.
// Touch FT6336U: G_CTRL (0x86) = 1 lets it drop to monitor mode by itself when idle (still wakes on touch).
// Never hibernate it: without a reset pin only a reboot brings it back.
static void lowPowerPeripherals()
{
    int r = settings::get().lora ? -1 : radio.sleep(); // with LoRa on the mesh core owns the radio
    Wire1.beginTransmission(0x38);
    Wire1.write(0x86);
    Wire1.write(0x01);
    int t = Wire1.endTransmission();
    diag::log("lowpower: lora sleep=%d, touch auto-monitor=%d", r, t);
}

// Screen on: 240 MHz while the user is touching/swiping (+1.5 s), 80 MHz otherwise. Never below 80
// (INT_WDT crash at 40 MHz in profiling). Through a PM lock: setCpuFrequencyMhz fights the power manager.
static void cpuClock()
{
    power::setFastCpu(power::screenOn() && lv_display_get_inactive_time(nullptr) < 1500);
}


// ---- iPhone notifications (ADR 0005): vibrate per app, show, calls buzz until answered (max 30 s) ----
static uint32_t s_callUid = 0, s_callStart = 0, s_callBuzz = 0;

static void notifications()
{
    notify::Events ev = notify::tick();
    if (ev.changed) ui::notificationsChanged();
    if (ev.callEnded || (s_callUid && !ui::callVisible())) {
        s_callUid = 0;
        haptics::stop();
        ui::closeCall();
    }
    if (ev.arrived) {
        const ancs::Item &it = *ev.arrived;
        bool call = it.category == ancs::kIncomingCall;
        if (call) {
            s_callUid = it.uid;
            s_callStart = s_callBuzz = millis();
            haptics::notification(ancs::Haptic::kCall);
            power::screenOnNow();
            ui::showCall(it);
        } else if (!modes::night() && !(it.flags & ancs::kSilent)) { // at night only calls (ADR 0005)
            haptics::notification(it.haptic);
            power::screenOnNow();
            ui::showNotification(it);
        }
    }
    if (lora::takeNewMessage() && !modes::night()) { // mesh text message: buzz + screen on
        haptics::notification(ancs::Haptic::kDefault);
        power::screenOnNow();
        ui::showTile(5);
    }
    if (s_callUid) {
        uint32_t ms = millis();
        if (ms - s_callStart > 30000) {
            s_callUid = 0;
            ui::closeCall();
        } else if (ms - s_callBuzz >= 1500) {
            s_callBuzz = ms;
            haptics::notification(ancs::Haptic::kCall);
            power::userActivity(); // keep the screen on while it rings
        }
    }
}

static void periodicWork()
{
    time_t t = clock_svc::now();
    if (clock_svc::valid()) {
        motion::updateSteps(local_time::localDayNumber(t));
        motion::setWakeGestureEnabled(!modes::night());
    }
    motion::persistSteps();
    modes::tick();
    if (!modes::nightSave()) clock_svc::tick(); // no Wi-Fi/NTP in night save: the RTC keeps time
}

void setup()
{
    Serial.begin(115200);
    diag::begin();
    diag::watchdog(30000); // generous during setup (Wi-Fi, LVGL init); 10 s once the loop runs
    ota::guardEarly(); // before any hardware init: a broken image must still be able to roll back
    settings::load();
    // Audio off: the I2S drivers of the speaker/mic hold an APB_FREQ_MAX PM lock forever, which makes
    // light sleep impossible (found with the PM lock dump, docs/ble.md). No GPS/NFC/SD on this model.
    LilyGoDeviceInitOptions init = instance.getDefaultInitOptions();
    init.initAudio = false;
    init.initGps = false;
    init.initNfc = false;
    init.initSd = false;
    init.scanI2c = false;
    init.initRadio = !settings::get().lora; // LoRa on: the Meshtastic core drives the SX1262 itself
    instance.begin(init);
    beginLvglHelper(instance);

    clock_svc::begin();
    motion::begin();
    haptics::begin();
    lowPowerPeripherals();
    power::begin();
    power::setBackgroundWork(periodicWork);
    periodicWork();
    ui::begin();
    notify::begin();
    if (settings::get().lora) lora::begin();
    diag::log("fw=%s accel=%s", ota::version(), motion::modelName());
    // Loop watchdog (5 s): a hang panics and reboots instead of freezing the watch; diag logs the stage.
    // power.cpp unsubscribes it around light sleep.
    const esp_task_wdt_config_t wdt = {.timeout_ms = 8000, .idle_core_mask = 0, .trigger_panic = true};
    esp_task_wdt_reconfigure(&wdt); // panic -> reboot (the Arduino default may only print a warning)
    enableLoopWDT();
    diag::watchdog(10000);
}

void loop()
{
    static uint32_t lastUi = 0;
    diag::feed();
    diag::stage(diag::Stage::BoardLoop);
    instance.loop();
    diag::stage(diag::Stage::OtaTick);
    ota::tick();

    notifications();
    cpuClock();
    static bool wasOn = true;
    if (wasOn && !power::screenOn()) ui::closePopups(); // next wake starts at the face
    wasOn = power::screenOn();
    if (power::screenOn()) {
        diag::stage(diag::Stage::LvglHandler);
        // render on LVGL's own schedule and let the CPU idle in between (was a busy loop: ~16 % more drain)
        uint32_t next = lv_timer_handler();
        uint32_t pause = next < 15 ? next : 15; // cap keeps touch responsive
        if (pause) vTaskDelay(pdMS_TO_TICKS(pause));
        if (millis() - lastUi > 1000) {
            lastUi = millis();
            diag::stage(diag::Stage::PeriodicWork);
            periodicWork();
            diag::stage(diag::Stage::UiUpdate);
            ui::update();
        }
    }
    diag::stage(diag::Stage::PowerTick);
    power::tick(); // enters light sleep while the screen is off
}
