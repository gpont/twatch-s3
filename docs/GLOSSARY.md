# Glossary

| Term | Meaning |
| --- | --- |
| **Face** | The main screen with the time. Always shows time, battery and steps. The selected face is stored in NVS. |
| **Tile / screen** | One of the swipeable screens around the face: notifications (up), settings (left), steps + system info (right). |
| **ANCS** | Apple Notification Center Service: the iPhone's built-in BLE service that shares notifications with a paired accessory. |
| **Bond** | The stored BLE pairing with the iPhone (kept in NVS by NimBLE). |
| **Unread** | A notification that arrived live (not replayed on connect) and was not looked at yet: the list was not opened and its card was not tapped. |
| **Night** | 22:30–07:30 local time: no wrist-raise wake, only calls vibrate. |
| **Night power save** | Optional (setting) during the night: BLE off, manual light sleep, 10 min timer wakes, dim 5 s screen, no NTP. |
| **Manual sleep** | `esp_light_sleep_start()` from the loop while the screen is off; used when notifications are off. |
| **Auto sleep** | ESP-IDF power-manager light sleep between FreeRTOS ticks and BLE events; used while notifications are on. |
| **Haptic pattern** | A sequence of DRV2605 library effects and pauses, chosen per app bundle id. |
| **Accel** | The accelerometer: BMA423 or BMA456 depending on the production batch (LilyGoLib detects it at boot). |
| **Pending image** | A freshly uploaded firmware that has not run 30 s yet; 3 failed boots roll back to the previous slot. |
