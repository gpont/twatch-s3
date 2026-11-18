#include "word_clock.h"
#include <string.h>
#include "ru_text.h"

namespace word_clock {

static const char *const kUnits[20] = {"", "ОДНА", "ДВЕ", "ТРИ", "ЧЕТЫРЕ", "ПЯТЬ", "ШЕСТЬ", "СЕМЬ", "ВОСЕМЬ", "ДЕВЯТЬ", "ДЕСЯТЬ",
                                       "ОДИННАДЦАТЬ", "ДВЕНАДЦАТЬ", "ТРИНАДЦАТЬ", "ЧЕТЫРНАДЦАТЬ", "ПЯТНАДЦАТЬ",
                                       "ШЕСТНАДЦАТЬ", "СЕМНАДЦАТЬ", "ВОСЕМНАДЦАТЬ", "ДЕВЯТНАДЦАТЬ"};
static const char *const kTens[6] = {"", "", "ДВАДЦАТЬ", "ТРИДЦАТЬ", "СОРОК", "ПЯТЬДЕСЯТ"};
static const char *const kHours[13] = {"ДВЕНАДЦАТЬ", "ЧАС", "ДВА", "ТРИ", "ЧЕТЫРЕ", "ПЯТЬ", "ШЕСТЬ",
                                       "СЕМЬ", "ВОСЕМЬ", "ДЕВЯТЬ", "ДЕСЯТЬ", "ОДИННАДЦАТЬ", "ДВЕНАДЦАТЬ"};
// filler letters for unlit cells (no vowels-only runs, looks like a real word-clock grid)
static const char *const kFill = "АБВГДЕЖЗИКЛМНОПРСТУФХЦЧШЭЮЯ";

static int words(int hour24, int minute, const char *out[8])
{
    int n = 0, h12 = hour24 % 12;
    out[n++] = kHours[h12];
    if (h12 != 1) { // "ЧАС" alone for 1 o'clock
        int h = h12 == 0 ? 12 : h12;
        out[n++] = ru_text::plural(h) == ru_text::Few ? "ЧАСА" : "ЧАСОВ";
    }
    if (minute == 0) {
        out[n++] = "РОВНО";
    } else {
        if (minute < 20) out[n++] = kUnits[minute];
        else {
            out[n++] = kTens[minute / 10];
            if (minute % 10) out[n++] = kUnits[minute % 10];
        }
        ru_text::Plural p = ru_text::plural(minute);
        out[n++] = p == ru_text::One ? "МИНУТА" : p == ru_text::Few ? "МИНУТЫ" : "МИНУТ";
    }
    out[n++] = hour24 < 5 ? "НОЧИ" : hour24 < 12 ? "УТРА" : hour24 < 17 ? "ДНЯ" : "ВЕЧЕРА";
    return n;
}

void phrase(int hour24, int minute, char *out, size_t cap)
{
    const char *w[8];
    int n = words(hour24, minute, w);
    out[0] = 0;
    for (int i = 0; i < n; i++) {
        if (i) strncat(out, " ", cap - strlen(out) - 1);
        strncat(out, w[i], cap - strlen(out) - 1);
    }
}

static int letters(const char *utf8) { return (int)strlen(utf8) / 2; }

void layout(int hour24, int minute, Grid &g)
{
    const char *w[8];
    int n = words(hour24, minute, w); // 3..6 words, 7 rows
    int top = (kRows - n) / 2;
    uint32_t seed = (uint32_t)(hour24 * 60 + minute) * 2654435761u;
    const int fillCount = letters(kFill);
    for (int r = 0; r < kRows; r++) {
        g.lit[r] = 0;
        int wi = r - top;
        const char *word = (wi >= 0 && wi < n) ? w[wi] : nullptr;
        int wl = word ? letters(word) : 0;
        seed = seed * 1103515245u + 12345u;
        int start = word ? (int)((seed >> 16) % (uint32_t)(kCols - wl + 1)) : kCols;
        char *o = g.rows[r];
        for (int c = 0; c < kCols; c++) {
            if (word && c >= start && c < start + wl) {
                memcpy(o, word + (c - start) * 2, 2);
                g.lit[r] |= 1u << c;
            } else {
                seed = seed * 1103515245u + 12345u;
                memcpy(o, kFill + ((seed >> 16) % fillCount) * 2, 2);
            }
            o += 2;
        }
        *o = 0;
    }
}

} // namespace word_clock
