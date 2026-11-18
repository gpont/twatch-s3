#!/usr/bin/env bash
# Full 16 MB flash backup of the T-Watch S3.
# Usage: scripts/backup-flash.sh [PORT] [OUT_DIR]
#   PORT    defaults to the first /dev/cu.usbmodem* (macOS) or /dev/ttyACM* (Linux) device
#   OUT_DIR defaults to ~/twatch-backups (outside the repo on purpose)
set -euo pipefail

FLASH_SIZE=$((0x1000000)) # 16 MB

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

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  PORT="$(ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -n1 || true)"
fi
if [[ -z "$PORT" ]]; then
  echo "Watch not found on USB. Connect it via USB (enter download mode if needed) and retry." >&2
  exit 1
fi

OUT_DIR="${2:-$HOME/twatch-backups}"
mkdir -p "$OUT_DIR"
OUT="$OUT_DIR/twatchs3-backup-$(date +%Y%m%d-%H%M%S).bin"

echo "Port:   $PORT"
echo "Output: $OUT"
"${ESPTOOL[@]}" --chip esp32s3 --port "$PORT" --baud 921600 read-flash 0 "$FLASH_SIZE" "$OUT"

SIZE=$(stat -f%z "$OUT" 2>/dev/null || stat -c%s "$OUT")
if [[ "$SIZE" -ne "$FLASH_SIZE" ]]; then
  echo "ERROR: backup size $SIZE != expected $FLASH_SIZE bytes" >&2
  exit 1
fi

shasum -a 256 "$OUT" | tee "$OUT.sha256"
echo "OK: backup is $SIZE bytes. Power-cycle the watch (hold crown 6 s, then 2 s)."
