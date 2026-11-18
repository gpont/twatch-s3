#pragma once
#include <stdint.h>

// Night window in minutes of local day; may cross midnight (23:00-08:00). from == to means "no night".
struct NightWindow {
    int16_t from;
    int16_t to;

    bool contains(int minuteOfDay) const
    {
        if (from == to) return false;
        if (from < to) return minuteOfDay >= from && minuteOfDay < to;
        return minuteOfDay >= from || minuteOfDay < to;
    }
};
