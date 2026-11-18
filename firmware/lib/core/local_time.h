#pragma once
#include <stdint.h>
#include <time.h>

// Local-time helpers over the C library's POSIX TZ support (same code on ESP32 newlib and host).
namespace local_time {

// Central European Time: CET, CEST from last Sunday of March 02:00 to last Sunday of October 03:00
constexpr const char *kDefaultTz = "CET-1CEST,M3.5.0,M10.5.0/3";

struct Zone { const char *name; const char *posix; };
// Selectable zones (Settings → Часовой пояс); index 0 is the default
constexpr Zone kZones[] = {
    {"Берлин", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Лиссабон", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Лондон", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Стамбул", "<+03>-3"},
    {"Москва", "MSK-3"},
    {"Тбилиси", "<+04>-4"},
    {"Дубай", "<+04>-4"},
    {"Ереван", "<+04>-4"},
    {"Алматы", "<+05>-5"},
    {"Бангкок", "<+07>-7"},
    {"Токио", "JST-9"},
    {"Нью-Йорк", "EST5EDT,M3.2.0,M11.1.0"},
};
constexpr int kZoneCount = sizeof(kZones) / sizeof(kZones[0]);

void setTimezone(const char *posixTz);

// Days since 1970-01-01 in local time; changes exactly at local midnight.
int32_t localDayNumber(time_t utc);

// 0..1439
int32_t minutesOfDay(time_t utc);

// UTC calendar fields -> epoch seconds (timegm without depending on TZ). The RTC stores UTC.
time_t utcFromCivil(int year, int month, int day, int hour, int minute, int second);

} // namespace local_time
