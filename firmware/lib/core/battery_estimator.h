#pragma once
#include <stdint.h>

// Remaining-time estimate from the battery percentage trend over the last 6 hours of discharge.
// Unknown (-1) while charging or with less than 2 hours of discharge data.
class BatteryEstimator {
public:
    static constexpr uint32_t kWindowSec = 6 * 3600;
    static constexpr uint32_t kMinSpanSec = 2 * 3600;
    static constexpr int kMaxSamples = 16;

    void addSample(uint32_t uptimeSec, uint8_t percent, bool charging);
    int32_t minutesLeft() const;

private:
    struct Sample { uint32_t t; uint8_t pct; };
    Sample samples_[kMaxSamples];
    int count_ = 0;
};
