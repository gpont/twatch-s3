#!/usr/bin/env bash
# Restore a full flash image (own backup or the official factory image) to the T-Watch S3.
# Usage: scripts/restore-flash.sh IMAGE.bin [PORT]
set -euo pipefail

IMAGE="${1:?Usage: $0 IMAGE.bin [PORT]}"
[[ -f "$IMAGE" ]] || { echo "No such file: $IMAGE" >&2; exit 1; }

if [[ -f "$IMAGE.sha256" ]]; then
  echo "Verifying checksum..."
  (cd "$(dirname "$IMAGE")" && shasum -a 256 -c "$(basename "$IMAGE").sha256")
fi

if command -v esptool >/dev/null 2>&1; then
  ESPTOOL=(esptool)
elif command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL=(esptool.py)
elif command -v uvx >/dev/null 2>&1; then
  ESPTOOL=(uvx esptool)
else
  echo "esptool not found. Install one of: 'brew install esptool' or 'uv tool install esptool'." >&2
  exit 1
fi

PORT="${2:-$(ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -n1 || true)}"
[[ -n "$PORT" ]] || { echo "Watch not found on USB." >&2; exit 1; }

echo "Writing $IMAGE to $PORT at offset 0x0..."
"${ESPTOOL[@]}" --chip esp32s3 --port "$PORT" --baud 921600 write-flash 0x0 "$IMAGE"
echo "Done. Power-cycle the watch: hold crown 6 s (off), then 2 s (on)."
