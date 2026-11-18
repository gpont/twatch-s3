#pragma once
#include <stdint.h>
#include <stddef.h>

// Diagnostics that survive light sleep and soft resets (panic/watchdog) in RTC memory:
// wake/sleep counters and a small ring log. Read over Wi-Fi on the charger: GET /debug, GET /log.
// A full power-off (crown 6 s) clears them.
namespace diag {

enum class Wake : uint8_t { Timer, Touch, Crown, Gesture, GpioUnknown, Other, Count };

struct Counters {
    uint32_t boots;
    uint32_t sleeps;
    uint32_t wakes[(int)Wake::Count];
    uint32_t screenOns;
    uint32_t lastSleepMs;   // millis() when we last went to sleep
    uint32_t longestAwakeMs;
};

// Breadcrumbs: where the main loop is right now. Kept in RTC memory, so after a watchdog reset
// the next boot logs the stage the firmware was stuck in.
enum class Stage : uint8_t {
    Boot, LoopStart, BoardLoop, OtaTick, LvglHandler, PeriodicWork, UiUpdate, PowerTick,
    PreSleep, Sleeping, WakeBoardLoop, WakeClearSensor, WakeScreenOn, Background, ScreenOff, Count
};
void stage(Stage s);
const char *stageName(Stage s);

// Hardware RTC watchdog: counts on the RTC slow clock, so it also runs during light sleep and
// catches anything (stuck loop, stuck sleep, interrupt storm). Resets the system; RTC memory
// (counters, ring log, stage) survives, so the next boot logs where it died.
void watchdog(uint32_t timeoutMs);  // (re)arm with a new timeout, also feeds
void feed();

void begin();                       // after Serial; counts boots, logs reset reason and stuck stage
void log(const char *fmt, ...);     // printf to Serial + ring log (timestamped)
void countSleep();
void countWake(Wake w);
void countScreenOn();
const Counters &counters();
const char *wakeName(Wake w);
size_t readLog(char *out, size_t cap); // oldest first

} // namespace diag
