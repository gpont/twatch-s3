#pragma once

// Night power save (Settings -> "Ночью экономия", fixed 22:30-07:30): BLE + notifications off, manual
// light sleep, timer wakes every 10 min, dim short screen, no NTP. Tap and crown still show the time.
namespace modes {

void tick();            // ~1/min and after background wakes: enter/leave the night window
void apply();           // re-apply BLE / sleep model after a settings change
bool night();           // inside the night window (quiet: only calls vibrate, no wrist gesture)
bool nightSave();       // power save active right now
const char *nightUntil(); // "07:30"

} // namespace modes
