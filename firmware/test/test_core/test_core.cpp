#include <unity.h>
#include <string.h>
#include "steps_day.h"
#include "battery_estimator.h"
#include "night.h"
#include "local_time.h"

void setUp() {}
void tearDown() {}

// ---------- StepsDay ----------
void test_steps_accumulate_within_day()
{
    StepsDay s;
    s.update(100, 0);
    s.update(100, 250);
    s.update(100, 400);
    TEST_ASSERT_EQUAL_UINT32(400, s.state().today);
}

void test_steps_sensor_reset_does_not_lose_steps()
{
    StepsDay s;
    s.update(100, 0);
    s.update(100, 1000);
    s.update(100, 30); // sensor counter restarted (reboot / power loss)
    TEST_ASSERT_EQUAL_UINT32(1030, s.state().today);
}

void test_steps_midnight_rolls_into_history()
{
    StepsDay s;
    s.update(100, 0);
    s.update(100, 5000);
    s.update(101, 5200); // first reading of the new day
    TEST_ASSERT_EQUAL_UINT32(200, s.state().today);
    TEST_ASSERT_EQUAL_UINT32(5000, s.state().history[0]); // yesterday
    TEST_ASSERT_EQUAL_INT32(101, s.state().day);
}

void test_steps_skipped_days_are_zero()
{
    StepsDay s;
    s.update(100, 0);
    s.update(100, 7000);
    s.update(103, 7000); // watch was off for two whole days
    TEST_ASSERT_EQUAL_UINT32(0, s.state().history[0]);    // day 102
    TEST_ASSERT_EQUAL_UINT32(0, s.state().history[1]);    // day 101
    TEST_ASSERT_EQUAL_UINT32(7000, s.state().history[2]); // day 100
}

void test_steps_history_keeps_seven_days()
{
    StepsDay s;
    s.update(0, 0);
    for (int d = 1; d <= 10; d++) s.update(d, d * 100);
    // history[0] = day 9 (900-800=100) ... each day had 100 steps
    for (int i = 0; i < StepsDay::kHistoryDays; i++) TEST_ASSERT_EQUAL_UINT32(100, s.state().history[i]);
}

void test_steps_restore_from_persisted_state()
{
    StepsDay::State st{};
    st.day = 200; st.today = 3000; st.lastSensor = 3000;
    StepsDay s(st);
    s.update(200, 10); // after reboot sensor starts from ~0
    TEST_ASSERT_EQUAL_UINT32(3010, s.state().today);
}

void test_steps_first_ever_reading_counts_nothing_before_it()
{
    StepsDay s; // fresh install
    s.update(50, 12345);
    TEST_ASSERT_EQUAL_UINT32(0, s.state().today);
}

// ---------- BatteryEstimator ----------
void test_battery_unknown_with_little_data()
{
    BatteryEstimator b;
    b.addSample(0, 100, false);
    b.addSample(3600, 95, false); // 1 h
    TEST_ASSERT_EQUAL_INT32(-1, b.minutesLeft());
}

void test_battery_estimate_after_two_hours()
{
    BatteryEstimator b;
    b.addSample(0, 80, false);
    b.addSample(3600, 76, false);
    b.addSample(7200, 72, false); // 4 %/h -> 72 % lasts 18 h
    TEST_ASSERT_EQUAL_INT32(18 * 60, b.minutesLeft());
}

void test_battery_uses_last_six_hours_only()
{
    BatteryEstimator b;
    b.addSample(0, 100, false);
    b.addSample(3600, 80, false); // heavy first hour, should be forgotten
    for (int h = 2; h <= 8; h++) b.addSample(h * 3600, 80 - (h - 1) * 2, false);
    // window 2h..8h: 78 -> 66 over 6 h = 2 %/h, 66 % -> 33 h
    TEST_ASSERT_EQUAL_INT32(33 * 60, b.minutesLeft());
}

void test_battery_charging_resets()
{
    BatteryEstimator b;
    b.addSample(0, 80, false);
    b.addSample(7200, 72, false);
    b.addSample(7300, 73, true);
    TEST_ASSERT_EQUAL_INT32(-1, b.minutesLeft());
}

// ---------- night window ----------
void test_night_window_across_midnight()
{
    NightWindow n{23 * 60, 8 * 60};
    TEST_ASSERT_TRUE(n.contains(23 * 60));
    TEST_ASSERT_TRUE(n.contains(2 * 60));
    TEST_ASSERT_FALSE(n.contains(8 * 60));
    TEST_ASSERT_FALSE(n.contains(14 * 60 + 37));
}

void test_night_window_same_day()
{
    NightWindow n{13 * 60, 14 * 60};
    TEST_ASSERT_TRUE(n.contains(13 * 60 + 30));
    TEST_ASSERT_FALSE(n.contains(14 * 60));
}

void test_night_window_empty_when_equal()
{
    NightWindow n{0, 0};
    TEST_ASSERT_FALSE(n.contains(0));
}

// ---------- local time (CET, DST) ----------
void test_local_day_changes_at_local_midnight_in_winter()
{
    local_time::setTimezone(local_time::kDefaultTz);
    // 2026-11-09 22:59:59 UTC = 23:59:59 CET; +1 s = 00:00 local next day
    int32_t a = local_time::localDayNumber(1794265199);
    int32_t b = local_time::localDayNumber(1794265200);
    TEST_ASSERT_EQUAL_INT32(a + 1, b);
}

void test_local_day_changes_at_local_midnight_in_summer()
{
    local_time::setTimezone(local_time::kDefaultTz);
    // 2026-07-01 21:59:59 UTC = 23:59:59 CEST
    int32_t a = local_time::localDayNumber(1782943199);
    int32_t b = local_time::localDayNumber(1782943200);
    TEST_ASSERT_EQUAL_INT32(a + 1, b);
}

void test_minutes_of_day_dst()
{
    local_time::setTimezone(local_time::kDefaultTz);
    TEST_ASSERT_EQUAL_INT32(14 * 60 + 37, local_time::minutesOfDay(1794231420)); // 2026-11-09 13:37 UTC -> 14:37 CET
    TEST_ASSERT_EQUAL_INT32(14 * 60 + 37, local_time::minutesOfDay(1782909420)); // 2026-07-01 12:37 UTC -> 14:37 CEST
}

void test_utc_from_civil()
{
    TEST_ASSERT_EQUAL_INT64(1794231420, (int64_t)local_time::utcFromCivil(2026, 11, 9, 13, 37, 0));
    TEST_ASSERT_EQUAL_INT64(0, (int64_t)local_time::utcFromCivil(1970, 1, 1, 0, 0, 0));
}

void test_zones_local_time()
{
    const time_t t = 1794231420; // 2026-11-09 13:37 UTC (winter)
    struct { const char *name; int minutes; } expect[] = {
        {"Берлин", 14 * 60 + 37}, {"Лондон", 13 * 60 + 37}, {"Москва", 16 * 60 + 37},
        {"Тбилиси", 17 * 60 + 37}, {"Токио", 22 * 60 + 37}, {"Нью-Йорк", 8 * 60 + 37}};
    for (auto &e : expect) {
        const char *posix = nullptr;
        for (auto &z : local_time::kZones) if (!strcmp(z.name, e.name)) posix = z.posix;
        TEST_ASSERT_NOT_NULL_MESSAGE(posix, e.name);
        local_time::setTimezone(posix);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(e.minutes, local_time::minutesOfDay(t), e.name);
    }
    local_time::setTimezone(local_time::kDefaultTz);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_zones_local_time);
    RUN_TEST(test_utc_from_civil);
    RUN_TEST(test_steps_accumulate_within_day);
    RUN_TEST(test_steps_sensor_reset_does_not_lose_steps);
    RUN_TEST(test_steps_midnight_rolls_into_history);
    RUN_TEST(test_steps_skipped_days_are_zero);
    RUN_TEST(test_steps_history_keeps_seven_days);
    RUN_TEST(test_steps_restore_from_persisted_state);
    RUN_TEST(test_steps_first_ever_reading_counts_nothing_before_it);
    RUN_TEST(test_battery_unknown_with_little_data);
    RUN_TEST(test_battery_estimate_after_two_hours);
    RUN_TEST(test_battery_uses_last_six_hours_only);
    RUN_TEST(test_battery_charging_resets);
    RUN_TEST(test_night_window_across_midnight);
    RUN_TEST(test_night_window_same_day);
    RUN_TEST(test_night_window_empty_when_equal);
    RUN_TEST(test_local_day_changes_at_local_midnight_in_winter);
    RUN_TEST(test_local_day_changes_at_local_midnight_in_summer);
    RUN_TEST(test_minutes_of_day_dst);
    return UNITY_END();
}
