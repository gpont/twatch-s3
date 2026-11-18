#include "battery_estimator.h"

void BatteryEstimator::addSample(uint32_t uptimeSec, uint8_t percent, bool charging)
{
    if (charging) { count_ = 0; return; }
    if (count_ == kMaxSamples) {
        for (int i = 1; i < count_; i++) samples_[i - 1] = samples_[i];
        count_--;
    }
    samples_[count_++] = {uptimeSec, percent};
    // drop samples older than the window, keeping at least one as the window start
    int drop = 0;
    while (drop < count_ - 1 && uptimeSec - samples_[drop + 1].t >= kWindowSec) drop++;
    if (drop) {
        for (int i = drop; i < count_; i++) samples_[i - drop] = samples_[i];
        count_ -= drop;
    }
}

int32_t BatteryEstimator::minutesLeft() const
{
    if (count_ < 2) return -1;
    const Sample &a = samples_[0], &b = samples_[count_ - 1];
    uint32_t span = b.t - a.t;
    if (span < kMinSpanSec || a.pct <= b.pct) return -1;
    // minutes = pct_now / (pct per minute)
    return (int32_t)((uint64_t)b.pct * span / 60 / (a.pct - b.pct));
}
