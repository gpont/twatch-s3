// 1. Big digits (Крупно): huge stacked hours/minutes, quiet side column, vertical step bar.
#include "faces.h"
#include "ru_text.h"
#include <stdio.h>

namespace {
lv_obj_t *s_date, *s_bat, *s_h, *s_m, *s_steps, *s_unread, *s_temp, *s_tempText, *s_barFill;

// Layout grid, measured on device screenshots (ink in pixels, not label boxes):
//   date/battery digits band 11 px; Oswald 104 digit ink 84 px; label->ink offsets below.
// Vertical: 15 | date 11 | 15 | hours 84 | 16 | minutes 84 | 15  = 240
// Horizontal: 12 px margins (ink 12..227). Right column: block 1 value top = hours top, block 3 caption
// baseline = minutes bottom, block 2 centred; the step bar spans hours top..minutes bottom.
constexpr int kDateInk = 15, kHoursInk = 41, kMinutesInk = 141, kBottom = 224;
constexpr int kDateOfs = 4, kDigitOfs = 2;          // label top -> ink top
constexpr int kValueOfs = 5, kCaptionBase = 11;      // Inter 20 value ink top / Inter 12 baseline
constexpr int kBlockSpan = 26;                       // value top -> caption baseline
constexpr int kColX = 134, kBarX = 224, kBarH = kBottom - kHoursInk + 1;

void block(lv_obj_t *p, int valueTop, lv_obj_t **value, lv_obj_t **caption, uint32_t color)
{
    *value = ui::label(p, &f_text_20, color, kColX, valueTop - kValueOfs);
    *caption = ui::label(p, &f_text_12, 0x8C8C8C, kColX, valueTop + kBlockSpan - kCaptionBase);
}

void build(lv_obj_t *p)
{
    lv_obj_set_style_bg_color(p, lv_color_hex(0x000000), 0);
    s_date = ui::label(p, &f_text_14, 0x9A9A9A, 11, kDateInk - kDateOfs);
    s_bat = ui::label(p, &f_text_14, 0x9A9A9A);
    lv_obj_align(s_bat, LV_ALIGN_TOP_RIGHT, -11, kDateInk - kDateOfs);
    s_h = ui::label(p, &f_oswald_104, 0xFFFFFF, 9, kHoursInk - kDigitOfs);
    s_m = ui::label(p, &f_oswald_104, 0xFF7A1A, 9, kMinutesInk - kDigitOfs);
    const int top1 = kHoursInk, top3 = kBottom - kBlockSpan, top2 = (top1 + top3 + 1) / 2;
    lv_obj_t *cap;
    block(p, top1, &s_steps, &cap, 0xFFFFFF);
    lv_label_set_text(cap, "шагов");
    block(p, top2, &s_unread, &cap, 0xFF7A1A); // temporarily instead of the budget (until M4)
    lv_label_set_text(cap, "уведомл.");
    block(p, top3, &s_temp, &s_tempText, 0xFFFFFF);
    ui::box(p, kBarX, kHoursInk, 4, kBarH, 0x262626, 2);
    s_barFill = ui::box(p, kBarX, kBottom + 1, 4, 0, 0xFF7A1A, 2);
}

void update(const FaceData &d)
{
    char b[32];
    if (d.timeValid) {
        lv_label_set_text_fmt(s_date, "%s, %d %s", ru_text::kDaysShort[d.wday], d.mday, ru_text::kMonthsGen[d.mon]);
        lv_label_set_text_fmt(s_h, "%02d", d.hour);
        lv_label_set_text_fmt(s_m, "%02d", d.minute);
    } else {
        lv_label_set_text(s_date, "время не задано");
        lv_label_set_text(s_h, "--");
        lv_label_set_text(s_m, "--");
    }
    lv_label_set_text_fmt(s_bat, "%u%%", d.battery);
    lv_obj_set_style_text_color(s_bat, lv_color_hex(d.batteryLow ? 0xFF3B30 : d.charging ? 0x3DDC84 : 0x9A9A9A), 0);
    ru_text::thousands(d.steps, b, sizeof(b));
    lv_label_set_text(s_steps, b);
    lv_label_set_text_fmt(s_unread, "%u", d.unreadCount);
    lv_obj_set_style_text_color(s_unread, lv_color_hex(d.unreadCount ? 0xFF7A1A : 0x5A5A5A), 0);
    if (d.weatherValid) { lv_label_set_text_fmt(s_temp, "%+d°", d.tempC); lv_label_set_text(s_tempText, d.weatherText); }
    else { lv_label_set_text(s_temp, "—"); lv_label_set_text(s_tempText, "погода"); }
    int h = (int)(ui::stepRatio(d) * kBarH);
    lv_obj_set_pos(s_barFill, kBarX, kBottom + 1 - h);
    lv_obj_set_height(s_barFill, h);
}
} // namespace

extern const FaceDef kFaceMinimal = {"Крупно", build, update};
