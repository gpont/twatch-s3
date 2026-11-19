#pragma once
#include "ancs.h"

// Screens around the face: notifications (up), settings (left), steps + system info below (right).
// Long press on the face switches to the next watch face (persisted).
namespace ui {

void begin();
void update();        // refresh values; call ~1 Hz while the screen is on
void setFace(int i);  // build watch face i (wraps), persist the choice
void nextFace();
void showTile(int i); // 0 face, 1 notifications, 2 steps, 3 settings, 4 system (steps screen, scrolled down), 5 LoRa
void scrollToRow(int i); // settings list (for screenshots)
int faceCount();

// iPhone notifications
void notificationsChanged();                  // store changed: rebuild the list
void showNotification(const ancs::Item &it);  // full-screen card (arrival or from the list)
void showCall(const ancs::Item &it);          // incoming call: decline / accept
void closeCall();
bool callVisible();
void closePopups();
const char *faceName(int i);

} // namespace ui
