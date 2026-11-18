#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ancs.h"

// Haptic patterns run on the DRV2605's own 8-slot sequencer, so playback never blocks.
// Step encoding (same as the DRV2605 waveform register): 1..123 = library effect,
// 0x80 | n = wait n * 10 ms. Patterns longer than 8 steps are truncated.
namespace haptics {

constexpr uint8_t wait10ms(uint8_t n) { return 0x80 | (n & 0x7F); }

void begin();
void play(const uint8_t *steps, size_t count);
void stop();

// built-in patterns
void lowBattery();
void tick(); // short confirmation click
void notification(ancs::Haptic h); // per-app pattern (ADR 0005)

} // namespace haptics
