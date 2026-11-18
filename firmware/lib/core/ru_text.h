#pragma once
#include <stdint.h>
#include <stddef.h>

// Small Russian text helpers shared by the watch faces.
namespace ru_text {

enum Plural { One, Few, Many }; // 1 минута / 2 минуты / 5 минут
Plural plural(uint32_t n);

void thousands(uint32_t n, char *out, size_t cap); // 5412 -> "5 412"
void euro(float v, char *out, size_t cap);         // 20.1 -> "+20,10 €", -3.456 -> "−3,46 €"

extern const char *const kDays[7];      // воскресенье..суббота
extern const char *const kDaysShort[7]; // вс..сб
extern const char *const kMonthsGen[12]; // января..декабря

} // namespace ru_text
