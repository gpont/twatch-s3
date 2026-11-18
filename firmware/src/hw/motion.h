#pragma once
#include <stdint.h>
#include "steps_day.h"

// Accelerometer: hardware step counter + wake gesture.
// BMA423: wrist-tilt interrupt. BMA456H (newer batches): SensorLib has no wrist feature,
// so double-tap is used as the wake gesture instead.
namespace motion {

void begin();               // after instance.begin()
void setWakeGestureEnabled(bool on); // off at night
bool wakeGestureEnabled();
void clearInterrupt();      // read & clear pending sensor IRQ before sleeping
const char *modelName();
uint32_t rawHardwareSteps(); // accelerometer counter as read now (diagnostics)
void setTestMotionIrq(bool on); // self-test: any-motion interrupt on INT1

// steps
void updateSteps(int32_t localDay);   // read hardware counter, roll the day
const StepsDay::State &steps();
void persistSteps(bool force = false); // NVS write, throttled unless forced

} // namespace motion
