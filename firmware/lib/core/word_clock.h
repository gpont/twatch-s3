#pragma once
#include <stdint.h>
#include <stddef.h>

// Russian "word clock": exact time as words ("ДВА ЧАСА ТРИДЦАТЬ СЕМЬ МИНУТ ДНЯ"), laid out on a
// 12x7 letter grid: one word per row, padded with filler letters, lit cells marked in a bitmask.
namespace word_clock {

constexpr int kCols = 12;
constexpr int kRows = 7;

struct Grid {
    char rows[kRows][kCols * 2 + 1]; // UTF-8: every Cyrillic letter is 2 bytes
    uint16_t lit[kRows];             // bit c = column c lit
};

void phrase(int hour24, int minute, char *out, size_t cap);
void layout(int hour24, int minute, Grid &g);

} // namespace word_clock
