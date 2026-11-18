// 6. 90s LCD (LCD 90-х): grey-green LCD, 7-segment digits with "ghost" segments, printed labels.
#include "faces.h"
#include <stdio.h>
#include <string.h>

namespace {
constexpr uint32_t kOn = 0x1D2318;
constexpr lv_opa_t kOffOpa = 12; // ~5 %: ghost segments barely visible

struct Digit { lv_obj_t *seg[7]; };
// a b c d e f g
const char *const kSegs[10] = {"abcdef", "bc", "abged", "abgcd", "fgbc", "afgcd", "afgedc", "abc", "abcdefg", "abcdfg"};

void makeDigit(lv_obj_t *p, Digit &dg, int x, int y, int W, int H, int T)
{
    int h2 = H / 2, g = 1;
    struct { int x, y, w, h; } r[7] = {
        {T + g, 0, W - 2 * T - 2 * g, T},                  // a
        {W - T, T / 2 + g, T, h2 - T / 2 - 2 * g},         // b
        {W - T, h2 + g, T, h2 - T / 2 - 2 * g},            // c
        {T + g, H - T, W - 2 * T - 2 * g, T},              // d
        {0, h2 + g, T, h2 - T / 2 - 2 * g},                // e
        {0, T / 2 + g, T, h2 - T / 2 - 2 * g},             // f
        {T + g, h2 - T / 2, W - 2 * T - 2 * g, T},         // g
    };
    for (int i = 0; i < 7; i++) dg.seg[i] = ui::box(p, x + r[i].x, y + r[i].y, r[i].w, r[i].h, kOn, T / 3);
}

void setDigit(Digit &dg, int v)
{
    const char *on = (v >= 0 && v <= 9) ? kSegs[v] : "";
    for (int i = 0; i < 7; i++) lv_obj_set_style_bg_opa(dg.seg[i], strchr(on, 'a' + i) ? LV_OPA_COVER : kOffOpa, 0);
}

Digit s_time[4], s_date[4], s_steps[5], s_budget[4], s_temp[2];
lv_obj_t *s_day, *s_bat, *s_budgetDot, *s_dateDot;

void row(lv_obj_t *p, Digit *d, int n, int x, int y, int W, int H, int T, int gap)
{
    for (int i = 0; i < n; i++) makeDigit(p, d[i], x + i * (W + gap), y, W, H, T);
}

void setNumber(Digit *d, int n, long v, bool leadingBlank)
{
    for (int i = n - 1; i >= 0; i--) {
        bool blank = leadingBlank && v == 0 && i < n - 1;
        setDigit(d[i], blank ? -1 : (int)(v % 10));
        v /= 10;
    }
}

lv_obj_t *lbl(lv_obj_t *p, const char *t, int x, int y)
{
    lv_obj_t *l = ui::label(p, &f_text_12, kOn, x, y);
    lv_label_set_text(l, t);
    return l;
}

void build(lv_obj_t *p)
{
    lv_obj_set_style_bg_color(p, lv_color_hex(0xC6D27A), 0);      // yellow-green reflective LCD
    lv_obj_set_style_bg_grad_color(p, lv_color_hex(0xA8B85C), 0);
    lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_VER, 0);
    s_day = ui::label(p, &f_text_20, kOn, 14, 16);
    row(p, s_date, 4, 120, 14, 16, 28, 4, 5);
    s_dateDot = ui::box(p, 156, 38, 4, 4, kOn);
    s_bat = ui::label(p, &f_text_14, kOn, 0, 50);
    lv_obj_align(s_bat, LV_ALIGN_TOP_RIGHT, -14, 50);
    makeDigit(p, s_time[0], 12, 72, 44, 80, 11);
    makeDigit(p, s_time[1], 62, 72, 44, 80, 11);
    ui::box(p, 113, 92, 10, 10, kOn);
    ui::box(p, 113, 124, 10, 10, kOn);
    makeDigit(p, s_time[2], 130, 72, 44, 80, 11);
    makeDigit(p, s_time[3], 180, 72, 44, 80, 11);
    ui::box(p, 10, 162, 220, 2, kOn);
    ui::box(p, 85, 168, 2, 64, kOn);
    ui::box(p, 163, 168, 2, 64, kOn);
    lbl(p, "ШАГИ", 10, 172);
    lbl(p, "€ ДЕНЬ", 93, 172);
    lbl(p, "°C", 172, 172);
    row(p, s_steps, 5, 8, 196, 11, 24, 3, 3);
    row(p, s_budget, 4, 92, 196, 12, 24, 3, 6);
    s_budgetDot = ui::box(p, 125, 217, 3, 3, kOn);
    row(p, s_temp, 2, 180, 196, 13, 24, 3, 4);
}

void update(const FaceData &d)
{
    static const char *const kDaysUp[7] = {"ВС", "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ"};
    if (d.timeValid) {
        lv_label_set_text(s_day, kDaysUp[d.wday]);
        setDigit(s_time[0], d.hour / 10); setDigit(s_time[1], d.hour % 10);
        setDigit(s_time[2], d.minute / 10); setDigit(s_time[3], d.minute % 10);
        setDigit(s_date[0], d.mday / 10); setDigit(s_date[1], d.mday % 10);
        setDigit(s_date[2], (d.mon + 1) / 10); setDigit(s_date[3], (d.mon + 1) % 10);
    } else {
        for (auto &dg : s_time) setDigit(dg, -1);
        for (auto &dg : s_date) setDigit(dg, -1);
    }
    lv_label_set_text_fmt(s_bat, "%s %u", LV_SYMBOL_BATTERY_FULL, d.battery);
    setNumber(s_steps, 5, d.steps > 99999 ? 99999 : d.steps, true);
    if (d.budgetValid) {
        long cents = (long)((d.budgetLeft < 0 ? -d.budgetLeft : d.budgetLeft) * 100 + 0.5f);
        setNumber(s_budget, 4, cents > 9999 ? 9999 : cents, false);
    } else {
        for (auto &dg : s_budget) setDigit(dg, -1);
    }
    if (d.weatherValid) setNumber(s_temp, 2, d.tempC < 0 ? -d.tempC : d.tempC, true);
    else for (auto &dg : s_temp) setDigit(dg, -1);
}
} // namespace

extern const FaceDef kFaceLcd = {"LCD 90-х", build, update};
