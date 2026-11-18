// 3. Dashboard (Панель): time on top, 2x2 tiles (steps, budget, weather, notifications).
#include "faces.h"
#include "ru_text.h"
#include <stdio.h>

namespace {
constexpr uint32_t kBg = 0x0E1116, kTile = 0x1A1F29, kMuted = 0x7D8598, kSub = 0x9AA2B4, kGreen = 0x3DDC84;
lv_obj_t *s_time, *s_date, *s_bat;
lv_obj_t *s_stepsV, *s_stepsSub, *s_stepsBar, *s_budgetV, *s_budgetSub, *s_weatherV, *s_weatherSub, *s_notifV, *s_notifSub;

lv_obj_t *tile(lv_obj_t *p, int x, int y, const char *title, lv_obj_t **value, lv_obj_t **sub)
{
    lv_obj_t *t = ui::box(p, x, y, 109, 77, kTile, 12);
    lv_obj_t *l = ui::label(t, &f_text_12, kMuted, 9, 6);
    lv_label_set_text(l, title);
    *value = ui::label(t, &f_digits_24, 0xFFFFFF, 9, 22);
    *sub = ui::label(t, &f_text_12, kSub, 9, 52);
    return t;
}

void build(lv_obj_t *p)
{
    lv_obj_set_style_bg_color(p, lv_color_hex(kBg), 0);
    s_time = ui::label(p, &f_digits_48, 0xFFFFFF, 10, 8);
    s_date = ui::label(p, &f_text_14, 0x8B93A5, 0, 16);
    lv_obj_align(s_date, LV_ALIGN_TOP_RIGHT, -10, 16);
    s_bat = ui::label(p, &f_text_14, kGreen, 0, 38);
    lv_obj_align(s_bat, LV_ALIGN_TOP_RIGHT, -10, 38);
    lv_obj_t *t = tile(p, 8, 74, "ШАГИ", &s_stepsV, &s_stepsSub);
    ui::box(t, 9, 68, 91, 4, 0x2A3140, 2);
    s_stepsBar = ui::box(t, 9, 68, 0, 4, kGreen, 2);
    tile(p, 123, 74, "БЮДЖЕТ", &s_budgetV, &s_budgetSub);
    tile(p, 8, 157, "ПОГОДА", &s_weatherV, &s_weatherSub);
    tile(p, 123, 157, "УВЕДОМЛЕНИЯ", &s_notifV, &s_notifSub);
}

void update(const FaceData &d)
{
    char b[32];
    if (d.timeValid) {
        lv_label_set_text_fmt(s_time, "%02d:%02d", d.hour, d.minute);
        lv_label_set_text_fmt(s_date, "%s, %d", ru_text::kDaysShort[d.wday], d.mday);
    } else {
        lv_label_set_text(s_time, "--:--");
        lv_label_set_text(s_date, "");
    }
    lv_label_set_text_fmt(s_bat, "%s %u%%", d.charging ? LV_SYMBOL_CHARGE : LV_SYMBOL_BATTERY_FULL, d.battery);
    lv_obj_set_style_text_color(s_bat, lv_color_hex(d.batteryLow ? 0xFF3B30 : kGreen), 0);

    ru_text::thousands(d.steps, b, sizeof(b));
    lv_label_set_text(s_stepsV, b);
    ru_text::thousands(d.goal, b, sizeof(b));
    lv_label_set_text_fmt(s_stepsSub, "из %s", b);
    lv_obj_set_width(s_stepsBar, (int)(91 * ui::stepRatio(d)));

    if (d.budgetValid) {
        ru_text::euro(d.budgetLeft, b, sizeof(b));
        lv_label_set_text(s_budgetV, b);
        lv_obj_set_style_text_color(s_budgetV, lv_color_hex(d.budgetLeft >= 0 ? 0x7EE08A : 0xFF6B5E), 0);
        lv_label_set_text_fmt(s_budgetSub, "из %d €", (int)d.budgetDay);
    } else {
        lv_label_set_text(s_budgetV, "—");
        lv_label_set_text(s_budgetSub, "нет данных");
    }
    if (d.weatherValid) {
        lv_label_set_text_fmt(s_weatherV, "%+d°", d.tempC);
        lv_label_set_text_fmt(s_weatherSub, "%s %d°/%d°", d.weatherText, d.maxC, d.minC);
    } else {
        lv_label_set_text(s_weatherV, "—");
        lv_label_set_text(s_weatherSub, "нет данных");
    }
    lv_label_set_text_fmt(s_notifV, "%u", d.notifCount);
    lv_label_set_text(s_notifSub, d.notifCount ? d.lastNotifFrom : "ничего нового");
}
} // namespace

extern const FaceDef kFacePanel = {"Панель", build, update};
