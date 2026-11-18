#include "motion.h"
#include <LilyGoLib.h>
#include <Preferences.h>
#include "diag.h"
#include "app/settings.h"

namespace motion {

static StepsDay s_steps;
static bool s_wake = true;
static uint32_t s_lastPersistMs = 0;
static uint32_t s_persistedToday = 0;
static constexpr uint32_t kPersistEveryMs = 5 * 60 * 1000;

uint32_t rawHardwareSteps();
static uint32_t hardwareSteps() { return rawHardwareSteps(); }

uint32_t rawHardwareSteps()
{
    switch (instance.detectedModel) {
    case DetectedModel::BMA423: return instance.bma423.getStepCount();
    case DetectedModel::BMA456H: return instance.bma456h.getStepCount();
    default: return 0;
    }
}

static void loadSteps()
{
    Preferences p;
    p.begin("steps", true);
    StepsDay::State st;
    if (p.getBytes("state", &st, sizeof(st)) == sizeof(st)) s_steps = StepsDay(st);
    p.end();
    s_persistedToday = s_steps.state().today;
}

void begin()
{
    // LilyGoLib enables any-motion, tap, step-detector and activity interrupts by default.
    // Any of them would wake the watch constantly, so keep only the wake gesture on INT1.
    SensorBMA4XX::MotionAxesConfig axes(1, 1, 1);
    // INT1 stays as LilyGoLib set it: active-HIGH push-pull. (Active-low makes SensorLib switch to open-drain,
    // and the board holds SENSOR_INT low, so the line reads "asserted" forever.) Sleep wakes on it via ext0.
    diag::log("bma int1: active-high push-pull, line idle=%d (expect 0)", digitalRead(SENSOR_INT));
    switch (instance.detectedModel) {
    case DetectedModel::BMA423: {
        auto &m = instance.bma423;
        bool am = m.enableAnyMotionDetection(axes, false, false);
        bool act = m.enableActivityRecognition(false, false);
        bool tap = m.enableTapDetector(false, false);
        bool plat = m.selectPlatform(SensorBMA423::Platform::WRISTBAND);
        bool tilt = m.enableTiltDetector(true, true);
        // step counter last: some feature-config writes above can reset it
        bool step = m.enableStepCounter(true, 1, false) && m.enableStepDetector(true, false);
        diag::log("bma423 cfg: anymotion_off=%d activity_off=%d tap_off=%d wristband=%d tilt=%d steps=%d raw=%lu",
                  am, act, tap, plat, tilt, step, (unsigned long)m.getStepCount());
        break;
    }
    case DetectedModel::BMA456H: {
        auto &m = instance.bma456h;
        bool am = m.enableAnyMotionDetection(axes, false, false);
        bool act = m.enableActivityRecognition(false, false);
        bool tap = m.enableTapDetector(true, true); // double-tap = wake
        bool step = m.enableStepCounter(true, 1, false) && m.enableStepDetector(true, false);
        diag::log("bma456h cfg: anymotion_off=%d activity_off=%d tap=%d steps=%d raw=%lu", am, act, tap, step,
                  (unsigned long)m.getStepCount());
        break;
    }
    default:
        diag::log("no accelerometer detected");
        break;
    }
    loadSteps();
}

void setTestMotionIrq(bool on)
{
    SensorBMA4XX::MotionAxesConfig axes(1, 1, 1);
    if (instance.sensor) instance.sensor->enableAnyMotionDetection(axes, on, on);
}

void setWakeGestureEnabled(bool on) { s_wake = on; }
bool wakeGestureEnabled() { return s_wake && settings::get().gestureWake && instance.detectedModel != DetectedModel::NONE; }

void clearInterrupt() { instance.loopSensor(); }

const char *modelName()
{
    switch (instance.detectedModel) {
    case DetectedModel::BMA423: return "BMA423";
    case DetectedModel::BMA456H: return "BMA456H";
    default: return "none";
    }
}

void updateSteps(int32_t localDay)
{
    if (instance.detectedModel == DetectedModel::NONE) return;
    int32_t before = s_steps.state().day;
    s_steps.update(localDay, hardwareSteps());
    if (before >= 0 && s_steps.state().day != before) persistSteps(true); // day rolled over
}

const StepsDay::State &steps() { return s_steps.state(); }

void persistSteps(bool force)
{
    uint32_t now = millis();
    if (!force && (now - s_lastPersistMs < kPersistEveryMs || s_steps.state().today == s_persistedToday)) return;
    Preferences p;
    p.begin("steps", false);
    p.putBytes("state", &s_steps.state(), sizeof(StepsDay::State));
    p.end();
    s_lastPersistMs = now;
    s_persistedToday = s_steps.state().today;
}

} // namespace motion
