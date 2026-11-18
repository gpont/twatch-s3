// 2. Game Boy: 4-shade DMG green, pixel font, pixel-art weather, segmented step bar.
#include "faces.h"
#include <stdio.h>

namespace {
constexpr uint32_t kDark = 0x0F380F, kMid = 0x306230, kLight = 0x8BAC0F, kBg = 0x9BBC0F;
constexpr int kSeg = 10, kPx = 4;
lv_obj_t *s_top, *s_bat, *s_time, *s_temp, *s_steps, *s_seg[kSeg], *s_budget, *s_budgetOk;
lv_obj_t *s_sprite;
uint8_t s_kind = 255;

// 12x8 maps, '#' outline, 'o' fill
const char *const kSun[8] = {"....#..#....", ".#..####..#.", "...######...", "..########..",
                             "..########..", "...######...", ".#..####..#.", "....#..#...."};
const char *const kCloud[8] = {"............", "...###......", "..#ooo#.##..", ".#ooooo#oo#.",
                               "#oooooooooo#", "#oooooooooo#", ".##########.", "............"};
const char *const kRain[8] = {"...###......", "..#ooo#.##..", ".#ooooo#oo#.", "#oooooooooo#",
                              ".##########.", "..#..#..#...", ".#..#..#....", "............"};
const char *const kSnow[8] = {"...###......", "..#ooo#.##..", ".#ooooo#oo#.", "#oooooooooo#",
                              ".##########.", "..o..o..o...", "...o..o..o..", "............"};

void drawSprite(uint8_t kind)
{
    if (kind == s_kind) return;
    s_kind = kind;
    lv_obj_clean(s_sprite);
    const char *const *map = kind == 0 ? kSun : kind == 2 ? kRain : kind == 3 ? kSnow : kCloud;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 12; x++) {
            char c = map[y][x];
            if (c == '.') continue;
            ui::box(s_sprite, x * kPx, y * kPx, kPx, kPx, c == '#' ? kDark : (kind == 0 ? kDark : kLight));
        }
}

void build(lv_obj_t *p)
{
    s_kind = 255;
    lv_obj_set_style_bg_color(p, lv_color_hex(kBg), 0);
    ui::box(p, 0, 0, 240, 24, kDark);
    s_top = ui::label(p, &f_pixel_16, kBg, 6, 4);
    s_bat = ui::label(p, &f_pixel_16, kBg, 0, 4);
    lv_obj_align(s_bat, LV_ALIGN_TOP_RIGHT, -6, 4);
    s_time = ui::label(p, &f_pixel_40, kDark, 0, 36);
    lv_obj_align(s_time, LV_ALIGN_TOP_MID, 0, 36);
    s_sprite = lv_obj_create(p);
    lv_obj_remove_style_all(s_sprite);
    lv_obj_set_size(s_sprite, 12 * kPx, 8 * kPx);
    lv_obj_set_pos(s_sprite, 40, 104);
    s_temp = ui::label(p, &f_pixel_16, kDark, 110, 114);
    ui::box(p, 0, 154, 240, 4, kMid);
    lv_obj_t *l = ui::label(p, &f_pixel_16, kDark, 6, 168);
    lv_label_set_text(l, "ШАГИ");
    s_steps = ui::label(p, &f_pixel_16, kDark, 0, 168);
    lv_obj_align(s_steps, LV_ALIGN_TOP_RIGHT, -6, 168);
    for (int i = 0; i < kSeg; i++) s_seg[i] = ui::box(p, 6 + i * 14, 192, 11, 14, kLight);
    s_budget = ui::label(p, &f_pixel_16, kDark, 6, 218);
    s_budgetOk = ui::label(p, &f_pixel_16, kDark, 0, 218);
    lv_obj_align(s_budgetOk, LV_ALIGN_TOP_RIGHT, -6, 218);
}

void update(const FaceData &d)
{
    static const char *const kDaysUp[7] = {"ВС", "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ"};
    if (d.timeValid) {
        lv_label_set_text_fmt(s_top, "%s %02d.%02d", kDaysUp[d.wday], d.mday, d.mon + 1);
        lv_label_set_text_fmt(s_time, "%02d:%02d", d.hour, d.minute);
    } else {
        lv_label_set_text(s_top, "--.--");
        lv_label_set_text(s_time, "--:--");
    }
    lv_label_set_text_fmt(s_bat, "%u%%", d.battery);
    drawSprite(d.weatherValid ? d.weatherKind : 1);
    if (d.weatherValid) lv_label_set_text_fmt(s_temp, "%+d°", d.tempC);
    else lv_label_set_text(s_temp, "--°");
    lv_label_set_text_fmt(s_steps, "%lu", (unsigned long)d.steps);
    int on = (int)(ui::stepRatio(d) * kSeg + 0.5f);
    for (int i = 0; i < kSeg; i++) lv_obj_set_style_bg_color(s_seg[i], lv_color_hex(i < on ? kDark : kLight), 0);
    if (d.budgetValid) {
        lv_label_set_text_fmt(s_budget, "€%+d", (int)d.budgetLeft);
        lv_label_set_text(s_budgetOk, d.budgetLeft >= 0 ? "ОК!" : "УПС");
    } else {
        lv_label_set_text(s_budget, "€ --");
        lv_label_set_text(s_budgetOk, "");
    }
}
} // namespace

extern const FaceDef kFaceGameboy = {"Game Boy", build, update};
