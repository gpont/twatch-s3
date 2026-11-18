#!/usr/bin/env bash
# Build the firmware and flash it over USB.
# Usage: scripts/flash_usb.sh [PORT]     (PORT is auto-detected: /dev/cu.usbmodem* on macOS, /dev/ttyACM* on Linux)
# No port? Enter download mode: hold BOOT, short-press the crown, release BOOT after 2-3 s.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FW="$ROOT/firmware"

if [[ ! -f "$FW/include/secrets.h" ]]; then
  echo "firmware/include/secrets.h is missing: run scripts/setup.sh first." >&2
  exit 1
fi

# PlatformIO: pioarduino needs Python 3.10-3.14, so prefer uvx with a pinned Python
if command -v uvx >/dev/null 2>&1; then
  PIO=(uvx --python 3.13 --from platformio pio)
elif command -v pio >/dev/null 2>&1; then
  PIO=(pio)
else
  echo "PlatformIO not found. Install uv (https://docs.astral.sh/uv/) or PlatformIO Core." >&2
  exit 1
fi

PORT="${1:-$(ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -n1 || true)}"
if [[ -z "$PORT" ]]; then
  echo "Watch not found on USB. Connect it (data cable) or enter download mode (see docs/flashing.md)." >&2
  exit 1
fi

echo "Building and flashing to $PORT (first build takes ~10 min)..."
"${PIO[@]}" run -d "$FW" -e twatch -t upload --upload-port "$PORT"
echo "Done. If the screen stays dark: hold the crown 6 s (off), then 2 s (on)."
