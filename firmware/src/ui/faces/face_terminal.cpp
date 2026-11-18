// 5. Terminal (Терминал): green phosphor on black, data as command output. (No scanlines/glow: LVGL can't do
// text glow cheaply; scanlines would need a full-screen overlay redraw.)
#include "faces.h"
#include "ru_text.h"
#include <stdio.h>
#include <string.h>

namespace {
constexpr uint32_t kGreen = 0x4DFF4D, kDim = 0x2DC23D;
lv_obj_t *s_time, *s_date, *s_steps, *s_budget, *s_weather, *s_weather2, *s_cursor;

void build(lv_obj_t *p)
{
    lv_obj_set_style_bg_color(p, lv_color_hex(0x030803), 0);
    lv_label_set_text(ui::label(p, &f_mono_14, kDim, 8, 4), "user@twatch:~$ now");
    s_time = ui::label(p, &f_mono_54, kGreen, 6, 28);
    s_date = ui::label(p, &f_mono_14, kGreen, 8, 90);
    lv_label_set_text(ui::label(p, &f_mono_14, kDim, 8, 108), "-------------------------");
    s_steps = ui::label(p, &f_mono_14, kGreen, 8, 126);
    s_budget = ui::label(p, &f_mono_14, kGreen, 8, 144);
    s_weather = ui::label(p, &f_mono_14, kGreen, 8, 162);
    s_weather2 = ui::label(p, &f_mono_14, kDim, 8, 180);
    lv_label_set_text(ui::label(p, &f_mono_14, kDim, 8, 198), "$");
    s_cursor = ui::box(p, 24, 200, 9, 15, kGreen);
}

void update(const FaceData &d)
{
    if (d.timeValid) {
        lv_label_set_text_fmt(s_time, "%02d:%02d", d.hour, d.minute);
    } else {
        lv_label_set_text(s_time, "--:--");
    }
    char bars[6];
    int full = (d.battery + 10) / 20;
    for (int i = 0; i < 5; i++) bars[i] = i < full ? '#' : '.';
    bars[5] = 0;
    if (d.timeValid) lv_label_set_text_fmt(s_date, "%s %02d.%02d [%s] %u%%%s", ru_text::kDaysShort[d.wday], d.mday, d.mon + 1, bars, d.battery, d.charging ? "+" : "");
    else lv_label_set_text_fmt(s_date, "--.-- [%s] %u%%", bars, d.battery);
    char bar[11];
    int n = (int)(ui::stepRatio(d) * 10 + 0.5f);
    for (int i = 0; i < 10; i++) bar[i] = i < n ? '#' : '-';
    bar[10] = 0;
    lv_label_set_text_fmt(s_steps, "шаги   [%s] %lu", bar, (unsigned long)d.steps);
    if (d.budgetValid) lv_label_set_text_fmt(s_budget, "бюджет  %+.2f€ / %.2f", d.budgetLeft, d.budgetDay);
    else lv_label_set_text(s_budget, "бюджет  нет данных");
    if (d.weatherValid) {
        lv_label_set_text_fmt(s_weather, "погода  %+d° %s", d.tempC, d.weatherText);
        lv_label_set_text_fmt(s_weather2, "        макс %d° мин %d°", d.maxC, d.minC);
    } else {
        lv_label_set_text(s_weather, "погода  нет данных");
        lv_label_set_text(s_weather2, "");
    }
    static bool blink = false;
    blink = !blink;
    lv_obj_set_style_bg_opa(s_cursor, blink ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}
} // namespace

extern const FaceDef kFaceTerminal = {"Терминал", build, update};
