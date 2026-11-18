#pragma once
#include <stdint.h>

// Daily step accounting on top of the accelerometer's cumulative hardware counter.
// The hardware counter is never reset by us: deltas are accumulated, a counter that
// went backwards (sensor reset / reboot) is treated as a fresh start from 0.
class StepsDay {
public:
    static constexpr int kHistoryDays = 7;

    struct State {
        int32_t day = -1;          // local day number (local_time::localDayNumber); -1 = never initialised
        uint32_t today = 0;
        uint32_t lastSensor = 0;
        uint32_t history[kHistoryDays] = {}; // [0] = yesterday, [1] = day before, ...
    };

    StepsDay() = default;
    explicit StepsDay(const State &restored) : s_(restored) {}

    void update(int32_t day, uint32_t sensorCount);
    const State &state() const { return s_; }

private:
    void rollTo(int32_t day);
    State s_;
};
