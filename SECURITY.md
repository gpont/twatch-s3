# Security policy

## Reporting a vulnerability
Please **do not open a public issue** for security problems. Use GitHub's
[private vulnerability reporting](https://github.com/gpont/twatch-s3/security/advisories/new) instead.
You will get an answer as soon as possible; this is a hobby project, so please allow a few days.

## Scope and known design limits
- **The HTTP server is for development.** It runs only while the watch is charging, on your local
  Wi-Fi, without TLS. Firmware upload and debug actions need HTTP basic auth with `OTA_PASSWORD`;
  read-only endpoints (`/info`, `/log`, `/batlog`, `/stats`, `/screenshot`) do not. Use a strong OTA
  password and a trusted network.
- **Wi-Fi credentials are compiled into the firmware** (`firmware/include/secrets.h`). Do not share
  binaries built with your secrets.
- BLE pairing with the iPhone uses LE Secure Connections ("Just Works", no PIN: the watch has no keypad).
- Notification content is kept in RAM only (last 20) and is cleared on reboot.
