#pragma once
#include <time.h>

// Time: PCF8563 RTC (stores UTC) is the source of truth; NTP over Wi-Fi corrects it
// at boot and every 6 h when credentials are configured. Wi-Fi is off otherwise.
namespace clock_svc {

void begin();         // RTC -> system time, time zone from settings (default CET)
void tick();          // non-blocking NTP state machine
bool valid();         // false if the RTC lost power and no NTP sync happened yet
bool busy();          // Wi-Fi in use: do not light-sleep
void requestSync();
void applyTimezone();  // from settings (call after changing it)
time_t lastSync();     // epoch of the last successful NTP sync, 0 = never (this boot)
bool syncing();
time_t now();

} // namespace clock_svc
