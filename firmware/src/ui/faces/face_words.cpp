// 7. Word clock (Словами): exact time as Russian words lit on a 12x7 letter grid (lib/core/word_clock).
#include "faces.h"
#include "ru_text.h"
#include "word_clock.h"
#include <stdio.h>
#include <string.h>

namespace {
constexpr uint32_t kDim = 0x2B2B33, kLit = 0xFFE7BD;
lv_obj_t *s_cell[word_clock::kRows][word_clock::kCols];
lv_obj_t *s_top, *s_bat, *s_bottom;
int s_lastMinute = -1;

void build(lv_obj_t *p)
{
    s_lastMinute = -1;
    lv_obj_set_style_bg_color(p, lv_color_hex(0x0B0B0D), 0);
    s_top = ui::label(p, &f_text_14, 0x8A8A93, 10, 6);
    s_bat = ui::label(p, &f_text_14, 0x8A8A93);
    lv_obj_align(s_bat, LV_ALIGN_TOP_RIGHT, -10, 6);
    for (int r = 0; r < word_clock::kRows; r++)
        for (int c = 0; c < word_clock::kCols; c++) {
            s_cell[r][c] = ui::label(p, &f_mono_19, kDim, 6 + c * 19, 30 + r * 24);
            lv_obj_set_width(s_cell[r][c], 19);
            lv_obj_set_style_text_align(s_cell[r][c], LV_TEXT_ALIGN_CENTER, 0);
        }
    s_bottom = ui::label(p, &f_text_14, 0xC9B48F, 10, 206);
}

void update(const FaceData &d)
{
    char b[24];
    if (d.timeValid) lv_label_set_text_fmt(s_top, "%s, %d %s", ru_text::kDaysShort[d.wday], d.mday, ru_text::kMonthsGen[d.mon]);
    else lv_label_set_text(s_top, "время не задано");
    lv_label_set_text_fmt(s_bat, "%u%%", d.battery);
    ru_text::thousands(d.steps, b, sizeof(b));
    if (d.weatherValid) lv_label_set_text_fmt(s_bottom, "%s ш.   %+d°", b, d.tempC);
    else lv_label_set_text_fmt(s_bottom, "%s шагов", b);

    int minute = d.timeValid ? d.hour * 60 + d.minute : -2;
    if (minute == s_lastMinute) return; // the grid only changes once a minute
    s_lastMinute = minute;
    word_clock::Grid g;
    word_clock::layout(d.timeValid ? d.hour : 0, d.timeValid ? d.minute : 0, g);
    for (int r = 0; r < word_clock::kRows; r++)
        for (int c = 0; c < word_clock::kCols; c++) {
            char ch[3] = {g.rows[r][c * 2], g.rows[r][c * 2 + 1], 0};
            lv_label_set_text(s_cell[r][c], ch);
            bool lit = d.timeValid && (g.lit[r] & (1u << c));
            lv_obj_set_style_text_color(s_cell[r][c], lv_color_hex(lit ? kLit : kDim), 0);
        }
}
} // namespace

extern const FaceDef kFaceWords = {"Словами", build, update};
