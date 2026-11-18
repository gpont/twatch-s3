#include "modes.h"
#include "night.h"
#include "local_time.h"
#include "settings.h"
#include "hw/clock.h"
#include "hw/diag.h"
#include "hw/notify.h"
#include "hw/power.h"

namespace modes {

static const NightWindow kNight{22 * 60 + 30, 7 * 60 + 30}; // M4: from the server config
static bool s_save = false;

bool night()
{
    return clock_svc::valid() && kNight.contains(local_time::minutesOfDay(clock_svc::now()));
}

bool nightSave() { return s_save; }
const char *nightUntil() { return "07:30"; }

void apply()
{
    bool ble = settings::get().notifications && !s_save;
    notify::setEnabled(ble);
    power::setAutoSleep(ble);
    power::setNightSave(s_save);
}

void tick()
{
    bool want = settings::get().nightSave && night();
    if (want == s_save) return;
    s_save = want;
    diag::log("night save %s", want ? "on" : "off");
    apply();
}

} // namespace modes
