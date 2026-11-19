#include "ui.h"
#include <LilyGoLib.h>
#include <lvgl.h>
#include <stdio.h>
#include <time.h>
#include "common.h"
#include "faces/faces.h"
#include "app/modes.h"
#include "app/settings.h"
#include "hw/clock.h"
#include "hw/haptics.h"
#include "hw/motion.h"
#include "hw/notify.h"
#include "lora/mesh_core.h"
#include "hw/ota.h"
#include "hw/power.h"
#include "ru_text.h"
#include "local_time.h"

namespace ui {

static constexpr uint32_t kBg = 0x000000, kFg = 0xFFFFFF, kMuted = 0x8C8C8C, kAccent = 0xFF7A1A, kRow = 0x1A1D24;

static lv_obj_t *s_tv, *s_faceTile, *s_faceBox;
static int s_face = 0;
static lv_obj_t *s_chart, *s_stepsToday, *s_stepsGoal, *s_infoBattery, *s_infoDiag;
static lv_chart_series_t *s_series;
static lv_obj_t *s_notifList, *s_notifStatus, *s_notifEmpty;

// ---------- face ----------
static FaceData collect()
{
    FaceData d = {};
    time_t t = clock_svc::now();
    struct tm lt;
    localtime_r(&t, &lt);
    d.timeValid = clock_svc::valid();
    d.hour = lt.tm_hour; d.minute = lt.tm_min; d.wday = lt.tm_wday; d.mday = lt.tm_mday; d.mon = lt.tm_mon;
    const power::Battery &b = power::battery();
    d.battery = b.percent; d.charging = b.charging; d.batteryLow = b.low;
    d.steps = motion::steps().today;
    d.goal = settings::get().stepGoal;
    d.budgetValid = false; // M4: server state
    d.weatherValid = false;
    d.weatherText = "";
    const ancs::Store &ns = notify::store();
    d.notifCount = ns.count() > 255 ? 255 : ns.count();
    d.lastNotifFrom = ns.count() ? ns.at(0).app : "";
    d.unreadCount = ns.unreadCount() > 255 ? 255 : ns.unreadCount();
    return d;
}

static void onFaceLongPress(lv_event_t *)
{
    haptics::tick();
    nextFace();
}

// Face widgets must not swallow touches: long press anywhere has to reach the tile.
static void passTouches(lv_obj_t *o)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *c = lv_obj_get_child(o, i);
        lv_obj_remove_flag(c, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        passTouches(c);
    }
}

static void refreshFaceRow();

void setFace(int i)
{
    s_face = ((i % faces::kCount) + faces::kCount) % faces::kCount;
    lv_obj_clean(s_faceBox);
    lv_obj_remove_style_all(s_faceBox); // styles a face set on the container (e.g. LCD gradient) must not leak
    lv_obj_set_size(s_faceBox, 240, 240);
    lv_obj_set_style_bg_opa(s_faceBox, LV_OPA_COVER, 0);
    faces::kAll[s_face].build(s_faceBox);
    passTouches(s_faceBox);
    faces::kAll[s_face].update(collect());
    if (settings::get().face != s_face) {
        settings::get().face = s_face;
        settings::save();
    }
    refreshFaceRow();
}

void nextFace() { setFace(s_face + 1); }
int faceCount() { return faces::kCount; }
const char *faceName(int i) { return faces::kAll[((i % faces::kCount) + faces::kCount) % faces::kCount].name; }

// ---------- notifications (bottom) ----------
static constexpr uint32_t kDanger = 0xE5383B, kOk = 0x2DBE60, kSoft = 0xC8C8C8;
static lv_obj_t *s_popup, *s_call;
static uint32_t s_popupUid, s_callUid;

// a plain container that lets clicks through to its parent
static lv_obj_t *plain(lv_obj_t *parent, int w)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, LV_SIZE_CONTENT);
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    return o;
}

static lv_obj_t *textLine(lv_obj_t *parent, const lv_font_t *f, uint32_t color, int w, int lines, const char *txt)
{
    lv_obj_t *l = label(parent, f, color);
    lv_label_set_long_mode(l, lines ? LV_LABEL_LONG_MODE_DOTS : LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, w);
    if (lines) lv_obj_set_height(l, lines * lv_font_get_line_height(f));
    lv_label_set_text(l, txt);
    return l;
}

static void header(lv_obj_t *parent, int w, const ancs::Item &it, const lv_font_t *appFont)
{
    lv_obj_t *h = plain(parent, w);
    lv_obj_t *a = label(h, appFont, kAccent);
    lv_label_set_text(a, it.app);
    lv_obj_t *t = label(h, &f_text_12, kMuted);
    lv_label_set_text(t, it.when);
    lv_obj_align(t, LV_ALIGN_RIGHT_MID, 0, 0);
}

// ---- swipe left/right to dismiss (list rows and the card) ----
static lv_point_t s_swStart;
static bool s_swiping;
static int s_swDx;

static void finishDismiss(void *p)
{
    uint32_t uid = (uint32_t)(uintptr_t)p;
    if (s_popup && s_popupUid == uid) {
        lv_obj_delete(s_popup);
        s_popup = nullptr;
    }
    notify::dismiss(uid);
    haptics::tick();
    notificationsChanged();
}

static void setSwipe(lv_obj_t *o, int dx)
{
    int a = dx < 0 ? -dx : dx;
    lv_obj_set_style_translate_x(o, dx, 0);
    lv_obj_set_style_opa(o, a > 200 ? 55 : 255 - a, 0);
}

static void onSwipe(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;
    lv_obj_t *o = (lv_obj_t *)lv_event_get_current_target(e);
    lv_indev_t *in = lv_indev_active();
    if (!in) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    if (code == LV_EVENT_PRESSED) {
        s_swStart = p;
        s_swiping = false;
        s_swDx = 0;
        return;
    }
    int dx = p.x - s_swStart.x, dy = p.y - s_swStart.y;
    if (code == LV_EVENT_PRESSING) {
        if (!s_swiping && (dx > 12 || dx < -12) && (dx < 0 ? -dx : dx) > 2 * (dy < 0 ? -dy : dy)) s_swiping = true;
        if (s_swiping) { s_swDx = dx; setSwipe(o, dx); }
        return;
    }
    if (!s_swiping) return;
    if (code == LV_EVENT_RELEASED && (s_swDx > 80 || s_swDx < -80)) { // past a third: slide out and dismiss
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, o);
        lv_anim_set_values(&a, s_swDx, s_swDx > 0 ? 240 : -240);
        lv_anim_set_duration(&a, 120);
        lv_anim_set_exec_cb(&a, [](void *v, int32_t x) { setSwipe((lv_obj_t *)v, x); });
        lv_anim_set_user_data(&a, lv_event_get_user_data(e));
        lv_anim_set_completed_cb(&a, [](lv_anim_t *an) { lv_async_call(finishDismiss, lv_anim_get_user_data(an)); });
        lv_anim_start(&a);
    } else {
        setSwipe(o, 0); // not far enough: back in place
    }
}

static void onRowOpen(lv_event_t *e)
{
    if (s_swiping) return; // the release of a swipe is not a tap
    uint32_t uid = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (const ancs::Item *it = notify::store().find(uid)) showNotification(*it);
}

static void updateNotifStatus()
{
    notify::Link l = notify::link();
    lv_label_set_text(s_notifStatus, modes::nightSave() ? "ночь" : notify::linkText());
    lv_obj_set_style_text_color(s_notifStatus, lv_color_hex(l == notify::Link::kReady ? kOk : kMuted), 0);
    if (!notify::store().count())
        lv_label_set_text(s_notifEmpty, modes::nightSave() ? "Ночная экономия\nдо 7:30"
                                         : l == notify::Link::kOff ? "Выключены в настройках"
                                         : l == notify::Link::kReady ? "Пока пусто" : "Пока пусто\niPhone не на связи");
}

void notificationsChanged()
{
    lv_obj_clean(s_notifList);
    const ancs::Store &st = notify::store();
    for (int i = 0; i < st.count(); i++) {
        const ancs::Item &it = st.at(i);
        lv_obj_t *row = lv_obj_create(s_notifList);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 224, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, lv_color_hex(kRow), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2A2F3A), LV_STATE_PRESSED);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_style_pad_row(row, 2, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        header(row, 208, it, &f_text_12);
        textLine(row, &f_text_14, kFg, 208, 1, it.title[0] ? it.title : it.app);
        if (it.message[0]) textLine(row, &f_text_12, kMuted, 208, 2, it.message);
        lv_obj_add_event_cb(row, onRowOpen, LV_EVENT_CLICKED, (void *)(uintptr_t)it.uid);
        lv_obj_add_event_cb(row, onSwipe, LV_EVENT_ALL, (void *)(uintptr_t)it.uid);
    }
    if (st.count()) lv_obj_add_flag(s_notifEmpty, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(s_notifEmpty, LV_OBJ_FLAG_HIDDEN);
    updateNotifStatus();
}

static void closePopup()
{
    if (s_popup) lv_obj_delete(s_popup);
    s_popup = nullptr;
}

static void onPopupTap(lv_event_t *)
{
    if (s_swiping) return;
    notify::markRead(s_popupUid); // tapped = seen
    closePopup();
}

static void onPopupDelete(lv_event_t *)
{
    notify::dismiss(s_popupUid);
    closePopup();
    notificationsChanged();
}

static void onPopupPositive(lv_event_t *)
{
    notify::act(s_popupUid, true);
    notify::markRead(s_popupUid);
    closePopup();
}

static lv_obj_t *button(lv_obj_t *parent, int w, int h, uint32_t bg, const char *text, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, h / 2 < 12 ? h / 2 : 12, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = label(b, &f_text_14, kFg);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

static lv_obj_t *overlay()
{
    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 240, 240);
    lv_obj_set_style_bg_color(o, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

void showNotification(const ancs::Item &it)
{
    closePopup();
    s_popupUid = it.uid;
    s_popup = overlay();
    lv_obj_set_style_pad_hor(s_popup, 12, 0);
    lv_obj_set_style_pad_top(s_popup, 12, 0);
    lv_obj_set_style_pad_bottom(s_popup, 14, 0);
    lv_obj_set_style_pad_row(s_popup, 6, 0);
    lv_obj_set_flex_flow(s_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_popup, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_popup, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(s_popup, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_popup, onPopupTap, LV_EVENT_CLICKED, nullptr); // tap anywhere closes
    lv_obj_add_event_cb(s_popup, onSwipe, LV_EVENT_ALL, (void *)(uintptr_t)it.uid);
    header(s_popup, 216, it, &f_text_14);
    if (it.title[0]) textLine(s_popup, &f_text_20, kFg, 216, 0, it.title);
    if (it.message[0]) textLine(s_popup, &f_text_14, kSoft, 216, 0, it.message);
    // the notification's own actions from iOS (labels like "Approve" / "Deny"; fallback names)
    bool pos = it.flags & ancs::kPositiveAction, neg = it.flags & ancs::kNegativeAction;
    if (pos || neg) {
        lv_obj_t *row = plain(s_popup, 216);
        lv_obj_set_style_margin_top(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 8, 0);
        int w = pos && neg ? 104 : 216;
        if (neg) button(row, w, 44, 0x3A1E20, it.negative[0] ? it.negative : "Удалить", onPopupDelete, nullptr);
        if (pos) button(row, w, 44, 0x1E5C34, it.positive[0] ? it.positive : "OK", onPopupPositive, nullptr);
    }
    power::userActivity();
}

static void onCallButton(lv_event_t *e)
{
    notify::callAnswer(s_callUid, lv_event_get_user_data(e) != nullptr);
    closeCall();
}

void showCall(const ancs::Item &it)
{
    closePopup();
    closeCall();
    s_callUid = it.uid;
    s_call = overlay();
    lv_obj_remove_flag(s_call, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *cap = label(s_call, &f_text_14, kMuted);
    lv_label_set_text(cap, "Входящий звонок");
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_t *who = textLine(s_call, &f_text_20, kFg, 216, 0, it.title[0] ? it.title : it.app);
    lv_obj_set_style_text_align(who, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(who, LV_ALIGN_TOP_MID, 0, 60);
    bool canAccept = it.flags & ancs::kPositiveAction;
    lv_obj_t *no = button(s_call, canAccept ? 100 : 160, 52, kDanger, "Отклонить", onCallButton, nullptr);
    lv_obj_align(no, canAccept ? LV_ALIGN_BOTTOM_LEFT : LV_ALIGN_BOTTOM_MID, canAccept ? 14 : 0, -24);
    if (canAccept) {
        lv_obj_t *yes = button(s_call, 100, 52, kOk, "Принять", onCallButton, (void *)1);
        lv_obj_align(yes, LV_ALIGN_BOTTOM_RIGHT, -14, -24);
    }
    power::userActivity();
}

void closeCall()
{
    if (s_call) lv_obj_delete(s_call);
    s_call = nullptr;
}

bool callVisible() { return s_call != nullptr; }

void closePopups()
{
    closePopup();
    closeCall();
}

static void buildNotifications(lv_obj_t *t)
{
    lv_obj_t *title = label(t, &f_text_14, kMuted, 12, 8);
    lv_label_set_text(title, "Уведомления");
    s_notifStatus = label(t, &f_text_12, kMuted);
    lv_obj_align(s_notifStatus, LV_ALIGN_TOP_RIGHT, -12, 10);
    s_notifList = lv_obj_create(t);
    lv_obj_remove_style_all(s_notifList);
    lv_obj_set_size(s_notifList, 240, 206);
    lv_obj_set_pos(s_notifList, 0, 32);
    lv_obj_set_flex_flow(s_notifList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_notifList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_notifList, 6, 0);
    lv_obj_set_style_pad_bottom(s_notifList, 10, 0);
    lv_obj_set_scroll_dir(s_notifList, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_notifList, LV_SCROLLBAR_MODE_OFF);
    s_notifEmpty = label(t, &f_text_14, kMuted);
    lv_obj_set_style_text_align(s_notifEmpty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_notifEmpty);
    notificationsChanged();
}

// ---------- steps (right) ----------
static void buildSteps(lv_obj_t *t)
{
    lv_obj_t *title = label(t, &f_text_14, kMuted, 12, 8);
    lv_label_set_text(title, "Шаги");
    s_stepsToday = label(t, &f_digits_40, kFg, 12, 30);
    s_stepsGoal = label(t, &f_text_14, kMuted, 12, 78);
    s_chart = lv_chart_create(t);
    lv_obj_set_size(s_chart, 220, 110);
    lv_obj_align(s_chart, LV_ALIGN_TOP_MID, 0, 112);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(s_chart, 8); // 7 past days + today
    lv_chart_set_div_line_count(s_chart, 0, 0);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_pad_column(s_chart, 4, 0);
    s_series = lv_chart_add_series(s_chart, lv_color_hex(kAccent), LV_CHART_AXIS_PRIMARY_Y);
}

static void updateSteps()
{
    const StepsDay::State &st = motion::steps();
    char b[16], g[16];
    ru_text::thousands(st.today, b, sizeof(b));
    lv_label_set_text(s_stepsToday, b);
    ru_text::thousands(settings::get().stepGoal, g, sizeof(g));
    lv_label_set_text_fmt(s_stepsGoal, "из %s сегодня · 7 дней:", g);
    uint32_t maxV = settings::get().stepGoal;
    for (int i = 0; i < StepsDay::kHistoryDays; i++) if (st.history[i] > maxV) maxV = st.history[i];
    if (st.today > maxV) maxV = st.today;
    lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, maxV);
    for (int i = 0; i < StepsDay::kHistoryDays; i++) lv_chart_set_value_by_id(s_chart, s_series, i, st.history[StepsDay::kHistoryDays - 1 - i]);
    lv_chart_set_value_by_id(s_chart, s_series, 7, st.today);
    lv_chart_refresh(s_chart);
}

// ---------- system: below the steps chart on the same screen (scroll down) ----------
static void buildSystem(lv_obj_t *t)
{
    constexpr int y = 240;
    lv_obj_t *title = label(t, &f_text_14, kMuted, 12, y + 4);
    lv_label_set_text(title, "Система");
    s_infoBattery = label(t, &f_text_14, kFg, 12, y + 32);
    s_infoDiag = label(t, &f_text_12, kMuted, 12, y + 58);
    lv_obj_t *pad = box(t, 0, y + 150, 1, 1, kBg); // bottom margin under the last line
    (void)pad;
}

static void updateSystem()
{
    const power::Battery &b = power::battery();
    if (b.minutesLeft >= 0)
        lv_label_set_text_fmt(s_infoBattery, "Батарея %u%% · ещё ~%ld ч", b.percent, (long)(b.minutesLeft / 60));
    else
        lv_label_set_text_fmt(s_infoBattery, "Батарея %u%% · %u мВ%s", b.percent, b.millivolts, b.charging ? " · заряд" : "");
    lv_label_set_text_fmt(s_infoDiag, "iPhone: %s\nДатчик: %s\nЧасы: %s\nПрошивка: %s\nOTA: %s", notify::linkText(),
                          motion::modelName(), clock_svc::valid() ? "время ок" : "нет времени", ota::version(), ota::status());
}

// ---------- settings ----------
struct Row { lv_obj_t *value; void (*change)(); void (*show)(lv_obj_t *); };

static void showBright(lv_obj_t *v) { lv_label_set_text_fmt(v, "%u из 5", settings::get().brightness + 1); }
static void changeBright()
{
    Settings &s = settings::get();
    s.brightness = (s.brightness + 1) % sizeof(settings::kBrightnessLevels);
    instance.setBrightness(settings::backlight());
}
static void showTimeout(lv_obj_t *v) { lv_label_set_text_fmt(v, "%u с", settings::get().timeoutSec); }
static void changeTimeout()
{
    Settings &s = settings::get();
    size_t i = 0;
    while (i < sizeof(settings::kTimeouts) && settings::kTimeouts[i] != s.timeoutSec) i++;
    s.timeoutSec = settings::kTimeouts[(i + 1) % sizeof(settings::kTimeouts)];
}
static const uint16_t kGoals[] = {5000, 6000, 7000, 8000, 9000, 10000, 12000, 15000};
static void showGoal(lv_obj_t *v)
{
    char b[16];
    ru_text::thousands(settings::get().stepGoal, b, sizeof(b));
    lv_label_set_text(v, b);
}
static void changeGoal()
{
    Settings &s = settings::get();
    size_t i = 0;
    while (i < sizeof(kGoals) / sizeof(kGoals[0]) && kGoals[i] != s.stepGoal) i++;
    s.stepGoal = kGoals[(i + 1) % (sizeof(kGoals) / sizeof(kGoals[0]))];
}
static void showGesture(lv_obj_t *v) { lv_label_set_text(v, settings::get().gestureWake ? "вкл" : "выкл"); }
static void changeGesture() { settings::get().gestureWake = !settings::get().gestureWake; }
static void showTap(lv_obj_t *v) { lv_label_set_text(v, settings::get().tapWake ? "вкл" : "выкл"); }
static void changeTap() { settings::get().tapWake = !settings::get().tapWake; }
static void showVib(lv_obj_t *v)
{
    static const char *const n[] = {"слабая", "средняя", "сильная"};
    lv_label_set_text(v, n[settings::get().vibration]);
}
static void changeVib()
{
    settings::get().vibration = (settings::get().vibration + 1) % 3;
    haptics::tick(); // feel the new strength
}
static void showFace(lv_obj_t *v) { lv_label_set_text(v, faceName(s_face)); }
static void changeFace() { nextFace(); }
static void showTz(lv_obj_t *v)
{
    uint8_t i = settings::get().timezone;
    lv_label_set_text(v, local_time::kZones[i < local_time::kZoneCount ? i : 0].name);
}
static void changeTz()
{
    settings::get().timezone = (settings::get().timezone + 1) % local_time::kZoneCount;
    clock_svc::applyTimezone();
    update(); // faces show the new local time right away
}
static void showSync(lv_obj_t *v)
{
    time_t t = clock_svc::lastSync();
    if (clock_svc::syncing()) { lv_label_set_text(v, "…"); return; }
    if (!t) { lv_label_set_text(v, LV_SYMBOL_REFRESH); return; }
    struct tm lt;
    localtime_r(&t, &lt);
    lv_label_set_text_fmt(v, "%02d:%02d", lt.tm_hour, lt.tm_min);
}
static void changeSync() { clock_svc::requestSync(); }
static void showNotif(lv_obj_t *v) { lv_label_set_text(v, settings::get().notifications ? "вкл" : "выкл"); }
static void changeNotif()
{
    settings::get().notifications = !settings::get().notifications;
    modes::apply();
}
static void showNightSave(lv_obj_t *v) { lv_label_set_text(v, settings::get().nightSave ? "22:30–7:30" : "выкл"); }
static void changeNightSave()
{
    settings::get().nightSave = !settings::get().nightSave;
    modes::tick(); // may enter / leave right now
}
static void showReboot(lv_obj_t *v) { lv_label_set_text(v, LV_SYMBOL_REFRESH); }
static void changeReboot() { ESP.restart(); }

enum RowId { kRowBright, kRowTimeout, kRowGoal, kRowNotif, kRowNightSave, kRowGesture, kRowTap, kRowVib, kFaceRow, kRowTz, kRowSync, kRowReboot, kRowCount };
static Row s_rows[kRowCount];
static void refreshFaceRow() { if (s_rows[kFaceRow].value) s_rows[kFaceRow].show(s_rows[kFaceRow].value); }

static void onRowClick(lv_event_t *e)
{
    Row *r = (Row *)lv_event_get_user_data(e);
    r->change();
    settings::save();
    r->show(r->value);
}

static void addRow(lv_obj_t *list, int i, const char *name, void (*change)(), void (*show)(lv_obj_t *))
{
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 224, 40);
    lv_obj_set_style_bg_color(row, lv_color_hex(kRow), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x2A2F3A), LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *n = label(row, &f_text_14, kFg);
    lv_label_set_text(n, name);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *v = label(row, &f_text_14, kAccent);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, -12, 0);
    s_rows[i] = {v, change, show};
    show(v);
    lv_obj_add_event_cb(row, onRowClick, LV_EVENT_CLICKED, &s_rows[i]);
}

static void buildSettings(lv_obj_t *t)
{
    lv_obj_t *title = label(t, &f_text_14, kMuted, 12, 8);
    lv_label_set_text(title, "Настройки");
    lv_obj_t *list = lv_obj_create(t);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 240, 206);
    lv_obj_set_pos(list, 0, 32);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_style_pad_bottom(list, 10, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    addRow(list, kRowBright, "Яркость", changeBright, showBright);
    addRow(list, kRowTimeout, "Экран гаснет", changeTimeout, showTimeout);
    addRow(list, kRowGoal, "Цель шагов", changeGoal, showGoal);
    addRow(list, kRowNotif, "Уведомления", changeNotif, showNotif);
    addRow(list, kRowNightSave, "Ночью экономия", changeNightSave, showNightSave);
    addRow(list, kRowGesture, "Жест запястьем", changeGesture, showGesture);
    addRow(list, kRowTap, "Тап будит", changeTap, showTap);
    addRow(list, kRowVib, "Вибрация", changeVib, showVib);
    addRow(list, kFaceRow, "Циферблат", changeFace, showFace);
    addRow(list, kRowTz, "Часовой пояс", changeTz, showTz);
    addRow(list, kRowSync, "Синхр. время", changeSync, showSync);
    addRow(list, kRowReboot, "Перезагрузить", changeReboot, showReboot);
}

// ---------- LoRa / Meshtastic (bottom) ----------
static lv_obj_t *s_meshState, *s_meshNodes, *s_meshMsg, *s_meshToggle;

static void onMeshToggle(lv_event_t *)
{
    settings::get().lora = !settings::get().lora;
    settings::save();
    lv_label_set_text(lv_obj_get_child(s_meshToggle, 0), "Перезагрузка…");
    lv_refr_now(nullptr);
    delay(400);
    ESP.restart(); // the mesh core starts (or stays off) at boot
}

static void buildMesh(lv_obj_t *t)
{
    lv_obj_t *title = label(t, &f_text_14, kMuted, 12, 8);
    lv_label_set_text(title, "LoRa · Meshtastic");
    s_meshState = label(t, &f_text_20, kFg, 12, 34);
    s_meshNodes = label(t, &f_text_14, kMuted, 12, 62);
    s_meshMsg = textLine(t, &f_text_14, kSoft, 216, 3, "");
    lv_obj_set_pos(s_meshMsg, 12, 88);
    s_meshToggle = button(t, 216, 44, settings::get().lora ? 0x3A1E20 : 0x1E5C34,
                          settings::get().lora ? "Выключить LoRa" : "Включить LoRa", onMeshToggle, nullptr);
    lv_obj_align(s_meshToggle, LV_ALIGN_BOTTOM_MID, 0, -12);
}

static void updateMesh()
{
    lv_label_set_text(s_meshState, lora::status());
    if (lora::running())
        lv_label_set_text_fmt(s_meshNodes, "Узлов: %lu · сообщений: %lu", (unsigned long)lora::nodeCount(),
                              (unsigned long)lora::messages());
    else
        lv_label_set_text(s_meshNodes, settings::get().lora ? "" : "Радио выключено");
    char from[12], text[200];
    if (lora::lastMessage(from, sizeof(from), text, sizeof(text)))
        lv_label_set_text_fmt(s_meshMsg, "%s: %s", from, text);
    else
        lv_label_set_text(s_meshMsg, lora::running() ? "Сообщений пока нет" : "");
}

// ---------- shell: tiles around the face ----------
//         [notifications]
// [settings]  [face]  [steps + system]
//              [LoRa]
static lv_obj_t *s_tiles[6]; // 0 face, 1 notifications, 2 steps (+ system below), 3 settings, 4 = steps too, 5 LoRa

void scrollToRow(int i)
{
    if (i < 0 || i >= (int)(sizeof(s_rows) / sizeof(s_rows[0])) || !s_rows[i].value) return;
    lv_obj_scroll_to_view_recursive(lv_obj_get_parent(s_rows[i].value), LV_ANIM_OFF);
}

void showTile(int i)
{
    static const uint8_t col[6] = {1, 1, 2, 0, 2, 1}, row[6] = {1, 0, 1, 1, 1, 2};
    if (i < 0 || i > 5) i = 0;
    lv_tileview_set_tile_by_index(s_tv, col[i], row[i], LV_ANIM_OFF);
    if (i == 4) lv_obj_scroll_to_view(s_infoDiag, LV_ANIM_OFF);
}

void begin()
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(kBg), 0);
    s_tv = lv_tileview_create(scr);
    lv_obj_set_style_bg_color(s_tv, lv_color_hex(kBg), 0);
    lv_obj_set_scrollbar_mode(s_tv, LV_SCROLLBAR_MODE_OFF);

    s_tiles[1] = lv_tileview_add_tile(s_tv, 1, 0, LV_DIR_BOTTOM);
    s_tiles[3] = lv_tileview_add_tile(s_tv, 0, 1, LV_DIR_RIGHT);
    s_faceTile = s_tiles[0] = lv_tileview_add_tile(s_tv, 1, 1, LV_DIR_ALL);
    s_tiles[4] = s_tiles[2] = lv_tileview_add_tile(s_tv, 2, 1, LV_DIR_LEFT);
    lv_obj_set_scroll_dir(s_tiles[2], LV_DIR_VER); // steps on top, system info further down
    lv_obj_set_scrollbar_mode(s_tiles[2], LV_SCROLLBAR_MODE_OFF);
    s_tiles[5] = lv_tileview_add_tile(s_tv, 1, 2, LV_DIR_TOP);

    s_faceBox = lv_obj_create(s_faceTile);
    lv_obj_remove_style_all(s_faceBox);
    lv_obj_set_size(s_faceBox, 240, 240);
    lv_obj_remove_flag(s_faceBox, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_event_cb(s_faceTile, onFaceLongPress, LV_EVENT_LONG_PRESSED, nullptr);

    buildNotifications(s_tiles[1]);
    buildSteps(s_tiles[2]);
    buildSettings(s_tiles[3]);
    buildSystem(s_tiles[4]);
    buildMesh(s_tiles[5]);

    setFace(settings::get().face);
    showTile(0);
    update();
}

void update()
{
    faces::kAll[s_face].update(collect());
    lv_obj_t *active = lv_tileview_get_tile_active(s_tv);
    if (active == s_tiles[3] && s_rows[kRowSync].value) s_rows[kRowSync].show(s_rows[kRowSync].value);
    if (active == s_tiles[1]) {
        updateNotifStatus();
        notify::markAllRead(); // the list is on screen: everything counts as seen
    }
    if (active == s_tiles[5]) updateMesh();
    if (active == s_tiles[2]) {
        updateSteps();
        updateSystem();
    }
}

} // namespace ui
