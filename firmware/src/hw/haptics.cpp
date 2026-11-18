#include "haptics.h"
#include <LilyGoLib.h>
#include "app/settings.h"

namespace haptics {

void begin()
{
    instance.drv.selectLibrary(1); // ERM library (the T-Watch S3 motor is an ERM)
}

void play(const uint8_t *steps, size_t count)
{
    if (count > 8) count = 8;
    instance.drv.stop();
    for (size_t i = 0; i < count; i++) instance.drv.setWaveform(i, steps[i]);
    if (count < 8) instance.drv.setWaveform(count, 0); // end of sequence
    instance.drv.run();
}

void stop() { instance.drv.stop(); }

// DRV2605 ROM library: 1/2/3 = strong click 100/60/30 %, 47/48/49 = buzz 100/80/60 %
static uint8_t click() { static const uint8_t c[] = {3, 2, 1}; return c[settings::get().vibration]; }
static uint8_t buzz() { static const uint8_t b[] = {49, 48, 47}; return b[settings::get().vibration]; }

void lowBattery()
{
    const uint8_t b = buzz();
    const uint8_t p[] = {b, wait10ms(20), b, wait10ms(20), b};
    play(p, sizeof(p));
}

// DRV2605 ERM library: 15 = 750 ms alert, 82 = ramp up long smooth
void notification(ancs::Haptic h)
{
    const uint8_t c = click(), b = buzz();
    switch (h) {
    case ancs::Haptic::kTelegram: { const uint8_t p[] = {c, wait10ms(12), c}; play(p, sizeof(p)); break; }
    case ancs::Haptic::kWhatsApp: { const uint8_t p[] = {c, wait10ms(12), c, wait10ms(12), c}; play(p, sizeof(p)); break; }
    case ancs::Haptic::kSlack: { const uint8_t p[] = {b, wait10ms(35), b}; play(p, sizeof(p)); break; }
    case ancs::Haptic::kMail: { const uint8_t p[] = {15}; play(p, sizeof(p)); break; }
    case ancs::Haptic::kCalendar: { const uint8_t p[] = {82}; play(p, sizeof(p)); break; }
    case ancs::Haptic::kCall: { const uint8_t p[] = {47, 47, 47}; play(p, sizeof(p)); break; } // strong regardless
    default: { const uint8_t p[] = {b}; play(p, sizeof(p)); break; }
    }
}

void tick()
{
    const uint8_t p[] = {click()};
    play(p, sizeof(p));
}

} // namespace haptics
