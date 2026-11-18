#include "clock.h"
#include <LilyGoLib.h>
#include <Network.h> // explicit: PlatformIO's LDF misses WiFi's dependency on it
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include "local_time.h"
#include "app/settings.h"
#include "ota.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif

namespace clock_svc {

enum class State { Idle, Connecting, Syncing };
static State s_state = State::Idle;
static uint32_t s_stateSince = 0;
static uint32_t s_lastSyncMs = 0;
static time_t s_lastSyncEpoch = 0;

static const char *tz()
{
    uint8_t i = settings::get().timezone;
    return local_time::kZones[i < local_time::kZoneCount ? i : 0].posix;
}
static bool s_pending = true; // sync at boot
static bool s_valid = false;
static constexpr uint32_t kResyncMs = 6UL * 3600 * 1000;
static constexpr uint32_t kConnectTimeoutMs = 15000;
static constexpr uint32_t kSyncTimeoutMs = 15000;
static constexpr int kMinValidYear = 2025;

static void writeRtc(time_t t)
{
    struct tm u;
    gmtime_r(&t, &u);
    instance.rtc.setDateTime(RTC_DateTime(u.tm_year + 1900, u.tm_mon + 1, u.tm_mday, u.tm_hour, u.tm_min, u.tm_sec));
}

void begin()
{
    local_time::setTimezone(tz());
    RTC_DateTime d = instance.rtc.getDateTime();
    if (d.getYear() >= kMinValidYear && instance.rtc.isClockIntegrityGuaranteed()) {
        timeval tv{local_time::utcFromCivil(d.getYear(), d.getMonth(), d.getDay(), d.getHour(), d.getMinute(), d.getSecond()), 0};
        settimeofday(&tv, nullptr);
        s_valid = true;
    }
    Serial.printf("[clock] rtc %04u-%02u-%02u %02u:%02u:%02u UTC valid=%d\n",
                  d.getYear(), d.getMonth(), d.getDay(), d.getHour(), d.getMinute(), d.getSecond(), s_valid);
}

static void wifiOff()
{
    if (ota::networkActive()) return; // OTA mode on the charger owns Wi-Fi
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
}

void tick()
{
    uint32_t ms = millis();
    switch (s_state) {
    case State::Idle:
        if (strlen(WIFI_SSID) == 0) return;
        if (!s_pending && ms - s_lastSyncMs < kResyncMs) return;
        s_pending = false;
        s_lastSyncMs = ms;
        if (WiFi.status() != WL_CONNECTED && !ota::networkActive()) { // OTA mode connects on its own
            WiFi.mode(WIFI_STA);
            WiFi.begin(WIFI_SSID, WIFI_PASS);
        }
        s_state = State::Connecting;
        s_stateSince = ms;
        break;
    case State::Connecting:
        if (WiFi.status() == WL_CONNECTED) {
            sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
            configTzTime(tz(), "pool.ntp.org", "time.google.com");
            s_state = State::Syncing;
            s_stateSince = ms;
        } else if (ms - s_stateSince > kConnectTimeoutMs) {
            Serial.println("[clock] wifi connect timeout");
            wifiOff();
            s_state = State::Idle;
        }
        break;
    case State::Syncing:
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            time_t t = time(nullptr);
            writeRtc(t);
            s_valid = true;
            s_lastSyncEpoch = t;
            Serial.printf("[clock] ntp ok %ld\n", (long)t);
            wifiOff();
            s_state = State::Idle;
        } else if (ms - s_stateSince > kSyncTimeoutMs) {
            Serial.println("[clock] ntp timeout");
            wifiOff();
            s_state = State::Idle;
        }
        break;
    }
}

bool valid() { return s_valid; }
bool busy() { return s_state != State::Idle; }
void requestSync() { s_pending = true; }
void applyTimezone() { local_time::setTimezone(tz()); }
time_t lastSync() { return s_lastSyncEpoch; }
bool syncing() { return s_state != State::Idle; }
time_t now() { return time(nullptr); }

} // namespace clock_svc
