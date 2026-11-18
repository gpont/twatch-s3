// 4. Analog (Аналог): dial with ticks, three subdials (steps ring, budget, weather), date and battery in corners.
#include "faces.h"
#include "ru_text.h"
#include <stdio.h>

namespace {
constexpr uint32_t kBg = 0x0B0E16, kFg = 0xE9ECF3, kMuted = 0x8B93A5, kSub = 0x151A26;
lv_obj_t *s_scale, *s_hourHand, *s_minHand, *s_month, *s_bat, *s_date;
lv_obj_t *s_stepsArc, *s_stepsV, *s_budgetV, *s_weatherV, *s_weatherSub;

lv_obj_t *subdial(lv_obj_t *p, int cx, int cy)
{
    lv_obj_t *c = ui::box(p, cx - 24, cy - 24, 48, 48, kSub, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(0x2A3142), 0);
    return c;
}

lv_obj_t *centered(lv_obj_t *parent, const lv_font_t *f, uint32_t color, int dy)
{
    lv_obj_t *l = ui::label(parent, f, color);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, dy);
    return l;
}

void build(lv_obj_t *p)
{
    lv_obj_set_style_bg_color(p, lv_color_hex(kBg), 0);
    lv_obj_t *steps = subdial(p, 70, 120);
    s_stepsArc = lv_arc_create(steps);
    lv_obj_set_size(s_stepsArc, 40, 40);
    lv_obj_center(s_stepsArc);
    lv_arc_set_rotation(s_stepsArc, 270);
    lv_arc_set_bg_angles(s_stepsArc, 0, 360);
    lv_arc_set_range(s_stepsArc, 0, 1000);
    lv_obj_remove_style(s_stepsArc, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(s_stepsArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_stepsArc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_stepsArc, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_stepsArc, lv_color_hex(0x26303F), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_stepsArc, lv_color_hex(0x3DDC84), LV_PART_INDICATOR);
    s_stepsV = centered(steps, &f_text_12, 0xFFFFFF, 0);

    lv_obj_t *budget = subdial(p, 170, 120);
    s_budgetV = centered(budget, &f_text_12, 0x7EE08A, -6);
    lv_label_set_text(centered(budget, &f_text_12, kMuted, 9), "ещё");

    lv_obj_t *weather = subdial(p, 120, 170);
    s_weatherV = centered(weather, &f_text_14, 0xFFFFFF, -6);
    s_weatherSub = centered(weather, &f_text_12, kMuted, 10);

    s_date = ui::label(p, &f_text_14, kFg);
    lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, 56);
    s_month = ui::label(p, &f_text_14, kMuted, 8, 6);
    s_bat = ui::label(p, &f_text_14, kFg);
    lv_obj_align(s_bat, LV_ALIGN_TOP_RIGHT, -8, 6);

    s_scale = lv_scale_create(p);
    lv_obj_set_size(s_scale, 224, 224);
    lv_obj_center(s_scale);
    lv_scale_set_mode(s_scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(s_scale, 0, 60);
    lv_scale_set_total_tick_count(s_scale, 61);
    lv_scale_set_major_tick_every(s_scale, 5);
    lv_scale_set_angle_range(s_scale, 360);
    lv_scale_set_rotation(s_scale, 270);
    lv_scale_set_label_show(s_scale, false);
    lv_obj_set_style_length(s_scale, 5, LV_PART_ITEMS);
    lv_obj_set_style_length(s_scale, 11, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(s_scale, lv_color_hex(0x4A5163), LV_PART_ITEMS);
    lv_obj_set_style_line_width(s_scale, 1, LV_PART_ITEMS);
    lv_obj_set_style_line_color(s_scale, lv_color_hex(kFg), LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s_scale, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_scale, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scale, LV_OPA_TRANSP, 0); // drawn over the subdials: hands on top

    s_hourHand = lv_line_create(s_scale);
    lv_obj_set_style_line_width(s_hourHand, 6, 0);
    lv_obj_set_style_line_rounded(s_hourHand, true, 0);
    lv_obj_set_style_line_color(s_hourHand, lv_color_hex(kFg), 0);
    s_minHand = lv_line_create(s_scale);
    lv_obj_set_style_line_width(s_minHand, 4, 0);
    lv_obj_set_style_line_rounded(s_minHand, true, 0);
    lv_obj_set_style_line_color(s_minHand, lv_color_hex(kFg), 0);
    ui::box(p, 115, 115, 10, 10, 0xFF7A1A, LV_RADIUS_CIRCLE);
}

void update(const FaceData &d)
{
    if (d.timeValid) {
        lv_scale_set_line_needle_value(s_scale, s_hourHand, 56, (d.hour % 12) * 5 + d.minute / 12);
        lv_scale_set_line_needle_value(s_scale, s_minHand, 88, d.minute);
        lv_label_set_text_fmt(s_date, "%s %d", d.wday == 0 ? "ВС" : d.wday == 1 ? "ПН" : d.wday == 2 ? "ВТ" : d.wday == 3 ? "СР" : d.wday == 4 ? "ЧТ" : d.wday == 5 ? "ПТ" : "СБ", d.mday);
        static const char *const kMon[12] = {"янв", "фев", "мар", "апр", "мая", "июн", "июл", "авг", "сен", "окт", "ноя", "дек"};
        lv_label_set_text(s_month, kMon[d.mon]);
    }
    lv_label_set_text_fmt(s_bat, "%u%%", d.battery);
    lv_obj_set_style_text_color(s_bat, lv_color_hex(d.batteryLow ? 0xFF3B30 : kFg), 0);
    lv_arc_set_value(s_stepsArc, (int)(ui::stepRatio(d) * 1000));
    if (d.steps >= 1000) lv_label_set_text_fmt(s_stepsV, "%lu,%luк", (unsigned long)(d.steps / 1000), (unsigned long)(d.steps % 1000 / 100));
    else lv_label_set_text_fmt(s_stepsV, "%lu", (unsigned long)d.steps);
    if (d.budgetValid) lv_label_set_text_fmt(s_budgetV, "%+d €", (int)d.budgetLeft);
    else lv_label_set_text(s_budgetV, "—");
    if (d.weatherValid) { lv_label_set_text_fmt(s_weatherV, "%+d°", d.tempC); lv_label_set_text(s_weatherSub, d.weatherText); }
    else { lv_label_set_text(s_weatherV, "—"); lv_label_set_text(s_weatherSub, ""); }
}
} // namespace

extern const FaceDef kFaceAnalog = {"Аналог", build, update};
