#include "steps_day.h"

void StepsDay::rollTo(int32_t day)
{
    int32_t gap = day - s_.day; // >= 1
    if (gap > kHistoryDays) gap = kHistoryDays + 1;
    for (int32_t g = 0; g < gap; g++) {
        for (int i = kHistoryDays - 1; i > 0; i--) s_.history[i] = s_.history[i - 1];
        s_.history[0] = (g == 0) ? s_.today : 0; // first shift stores the finished day, the rest are empty days
    }
    s_.today = 0;
    s_.day = day;
}

void StepsDay::update(int32_t day, uint32_t sensorCount)
{
    if (s_.day < 0) { // first reading ever: establish the baseline only
        s_.day = day;
        s_.lastSensor = sensorCount;
        return;
    }
    if (day > s_.day) rollTo(day);

    uint32_t delta = sensorCount >= s_.lastSensor ? sensorCount - s_.lastSensor : sensorCount;
    s_.today += delta;
    s_.lastSensor = sensorCount;
}
