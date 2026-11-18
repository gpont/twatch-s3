#pragma once
#include <lvgl.h>
#include <stdint.h>
#include "fonts/fonts.h"

// Everything a watch face shows, refreshed ~1 Hz. Budget / weather stay invalid until the server (M4).
struct FaceData {
    bool timeValid;
    int hour, minute, wday, mday, mon; // local time; wday 0 = Sunday, mon 0 = January
    uint8_t battery;
    bool charging, batteryLow;
    uint32_t steps;
    uint16_t goal;
    bool budgetValid;
    float budgetLeft, budgetDay;      // EUR left today / daily allowance
    bool weatherValid;
    int tempC, maxC, minC;
    const char *weatherText;          // "облачно"
    uint8_t weatherKind;              // 0 sun, 1 cloud, 2 rain, 3 snow
    uint8_t notifCount;
    uint8_t unreadCount;              // live notifications not looked at yet
    const char *lastNotifFrom;        // app name of the newest notification
};

namespace ui {

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x = 0, int y = 0);
lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius = 0);
float stepRatio(const FaceData &d); // 0..1

} // namespace ui
