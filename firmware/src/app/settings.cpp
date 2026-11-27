#include "settings.h"
#include <Preferences.h>

namespace settings {

static Settings s;

void load()
{
    Preferences p;
    p.begin("settings", true);
    s.brightness = p.getUChar("bright", s.brightness);
    s.timeoutSec = p.getUChar("timeout", s.timeoutSec);
    s.stepGoal = p.getUShort("goal", s.stepGoal);
    s.face = p.getUChar("face", s.face);
    s.gestureWake = p.getBool("gesture", s.gestureWake);
    s.tapWake = p.getBool("tap", s.tapWake);
    s.vibration = p.getUChar("vib", s.vibration);
    s.timezone = p.getUChar("tz", s.timezone);
    s.notifications = p.getBool("notif", s.notifications);
    s.nightSave = p.getBool("nightsave", s.nightSave);
    s.lora = p.getBool("lora", s.lora);
    s.faceLongPress = p.getBool("facelp", s.faceLongPress);
    p.end();
    if (s.brightness >= sizeof(kBrightnessLevels)) s.brightness = 1;
    if (s.vibration > 2) s.vibration = 2;
}

void save()
{
    Preferences p;
    p.begin("settings", false);
    p.putUChar("bright", s.brightness);
    p.putUChar("timeout", s.timeoutSec);
    p.putUShort("goal", s.stepGoal);
    p.putUChar("face", s.face);
    p.putBool("gesture", s.gestureWake);
    p.putBool("tap", s.tapWake);
    p.putUChar("vib", s.vibration);
    p.putUChar("tz", s.timezone);
    p.putBool("notif", s.notifications);
    p.putBool("nightsave", s.nightSave);
    p.putBool("lora", s.lora);
    p.putBool("facelp", s.faceLongPress);
    p.end();
}

Settings &get() { return s; }

uint8_t backlight() { return kBrightnessLevels[s.brightness]; }

} // namespace settings
