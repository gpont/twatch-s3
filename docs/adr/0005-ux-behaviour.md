# 0005. UX behaviour: night, notifications, haptics, battery

Status: accepted, 2026-11-09; navigation and night window updated by ADR 0007 and later changes.

## Night (22:30–07:30)
- Wrist raise does not wake the screen — only tap and crown.
- Only calls vibrate; other notifications land silently in the list.
- Optional night power save (setting, on by default): BLE off, deeper sleep (see [GLOSSARY](../GLOSSARY.md)).

## Time
- The RTC is the source of truth (stored in UTC); NTP corrects it over Wi-Fi. Time zone from settings (POSIX TZ, DST aware).
- If the RTC lost power and there is no Wi-Fi: `--:--` and a warning.

## Battery
- Percent, charging state; ≤ 15 % → one vibration and a red indicator.
- Remaining time: moving average of the drain over 6 h, "—" until there are 2 h of data. Faces show only percent.

## Notifications
- Last 20 in RAM only. Title and text are shown in full on the card.
- New notification: vibrate, wake the screen, show the card. Tap closes, *Delete* or a sideways swipe dismisses
  (ANCS negative action: removed on the iPhone too).
- Calls: caller screen with Decline/Accept, strong buzz every 1.5 s until answered or ended, max 30 s.

## Default haptics
| App | Pattern |
| --- | --- |
| Telegram `ph.telegra.Telegraph` | 2 short clicks |
| WhatsApp `net.whatsapp.WhatsApp` | 3 short clicks |
| Slack `com.tinyspeck.chatlyio` | "knock-knock": 2 medium buzzes with a pause |
| Mail `com.apple.mobilemail`, Gmail `com.google.Gmail` | 1 long soft alert |
| Calendar `com.apple.mobilecal` | ramp up |
| Phone call | strong buzz every 1.5 s, max 30 s |
| Others | 1 medium buzz |
