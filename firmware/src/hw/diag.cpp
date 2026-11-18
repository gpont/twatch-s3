#include "diag.h"
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <stdarg.h>
#include <time.h>
#include "hal/wdt_hal.h"
#include "soc/rtc.h"

namespace diag {

static constexpr uint32_t kMagic = 0x7A7C5002;
static constexpr size_t kLogSize = 3072;

struct Persist {
    uint32_t magic;
    Counters c;
    uint8_t stage;
    uint32_t stageMs;
    uint32_t head;      // next write position in buf
    bool wrapped;
    char buf[kLogSize];
};
RTC_NOINIT_ATTR static Persist s_p;

static const char *resetReason(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_USB: return "usb";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    default: return "other";
    }
}

static void put(const char *s)
{
    for (; *s; s++) {
        s_p.buf[s_p.head++] = *s;
        if (s_p.head >= kLogSize) { s_p.head = 0; s_p.wrapped = true; }
    }
}

const char *stageName(Stage s)
{
    static const char *n[] = {"boot", "loop-start", "board-loop", "ota-tick", "lvgl-handler", "periodic-work", "ui-update",
                              "power-tick", "pre-sleep", "sleeping", "wake-board-loop", "wake-clear-sensor", "wake-screen-on",
                              "background", "screen-off"};
    return (uint8_t)s < (uint8_t)Stage::Count ? n[(uint8_t)s] : "?";
}

static wdt_hal_context_t s_rwdt = RWDT_HAL_CONTEXT_DEFAULT();

void watchdog(uint32_t timeoutMs)
{
    uint32_t ticks = (uint32_t)((uint64_t)timeoutMs * rtc_clk_slow_freq_get_hz() / 1000);
    wdt_hal_write_protect_disable(&s_rwdt);
    wdt_hal_config_stage(&s_rwdt, WDT_STAGE0, ticks, WDT_STAGE_ACTION_RESET_SYSTEM);
    wdt_hal_enable(&s_rwdt);
    wdt_hal_feed(&s_rwdt);
    wdt_hal_write_protect_enable(&s_rwdt);
}

void feed()
{
    wdt_hal_write_protect_disable(&s_rwdt);
    wdt_hal_feed(&s_rwdt);
    wdt_hal_write_protect_enable(&s_rwdt);
}

void stage(Stage s)
{
    s_p.stage = (uint8_t)s;
    s_p.stageMs = millis();
}

void begin()
{
    esp_reset_reason_t r = esp_reset_reason();
    bool fresh = s_p.magic != kMagic || r == ESP_RST_POWERON;
    uint8_t prevStage = s_p.stage;
    uint32_t prevMs = s_p.stageMs;
    if (fresh) {
        memset(&s_p, 0, sizeof(s_p));
        s_p.magic = kMagic;
    }
    s_p.c.boots++;
    log("boot #%lu reset=%s", (unsigned long)s_p.c.boots, resetReason(r));
    if (!fresh && (r == ESP_RST_TASK_WDT || r == ESP_RST_INT_WDT || r == ESP_RST_WDT || r == ESP_RST_PANIC || r == ESP_RST_SW))
        log("!!! previous run died in stage '%s' (entered at +%lus)", stageName((Stage)prevStage), (unsigned long)(prevMs / 1000));
    stage(Stage::Boot);
}

void log(const char *fmt, ...)
{
    char line[160];
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    int n = snprintf(line, sizeof(line), "%02d:%02d:%02d +%lus ", lt.tm_hour, lt.tm_min, lt.tm_sec, (unsigned long)(millis() / 1000));
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof(line) - n - 1, fmt, ap);
    va_end(ap);
    strlcat(line, "\n", sizeof(line));
    Serial.print(line);
    put(line);
}

void countSleep()
{
    uint32_t now = millis();
    s_p.c.sleeps++;
    s_p.c.lastSleepMs = now;
}

void countWake(Wake w) { s_p.c.wakes[(int)w]++; }

void countScreenOn() { s_p.c.screenOns++; }

const Counters &counters() { return s_p.c; }

const char *wakeName(Wake w)
{
    static const char *n[] = {"timer", "touch", "crown", "gesture", "gpio?", "other"};
    return n[(int)w];
}

size_t readLog(char *out, size_t cap)
{
    size_t n = 0;
    auto copy = [&](size_t from, size_t to) {
        for (size_t i = from; i < to && n + 1 < cap; i++) if (s_p.buf[i]) out[n++] = s_p.buf[i];
    };
    if (s_p.wrapped) copy(s_p.head, kLogSize);
    copy(0, s_p.head);
    out[n] = 0;
    return n;
}

} // namespace diag
