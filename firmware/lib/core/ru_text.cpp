#include "ru_text.h"
#include <stdio.h>
#include <string.h>

namespace ru_text {

const char *const kDays[7] = {"воскресенье", "понедельник", "вторник", "среда", "четверг", "пятница", "суббота"};
const char *const kDaysShort[7] = {"вс", "пн", "вт", "ср", "чт", "пт", "сб"};
const char *const kMonthsGen[12] = {"января", "февраля", "марта", "апреля", "мая", "июня",
                                    "июля", "августа", "сентября", "октября", "ноября", "декабря"};

Plural plural(uint32_t n)
{
    uint32_t d10 = n % 10, d100 = n % 100;
    if (d100 >= 11 && d100 <= 14) return Many;
    if (d10 == 1) return One;
    if (d10 >= 2 && d10 <= 4) return Few;
    return Many;
}

void thousands(uint32_t n, char *out, size_t cap)
{
    char digits[12];
    int len = snprintf(digits, sizeof(digits), "%lu", (unsigned long)n);
    size_t o = 0;
    for (int i = 0; i < len && o + 1 < cap; i++) {
        if (i > 0 && (len - i) % 3 == 0 && o + 1 < cap) out[o++] = ' ';
        if (o + 1 < cap) out[o++] = digits[i];
    }
    out[o] = 0;
}

void euro(float v, char *out, size_t cap)
{
    long cents = (long)(v * 100 + (v >= 0 ? 0.5f : -0.5f));
    const char *sign = cents >= 0 ? "+" : "−"; // U+2212
    if (cents < 0) cents = -cents;
    snprintf(out, cap, "%s%ld,%02ld €", sign, cents / 100, cents % 100);
}

} // namespace ru_text
