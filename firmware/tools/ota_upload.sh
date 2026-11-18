#!/usr/bin/env bash
# Build (incremental, same build dir as USB flashing) and upload over Wi-Fi via HTTP.
# Watch must be on a charger in the same network. Settings: ota_secrets.ini (gitignored).
# Uses curl over HTTP (not espota): on macOS the "Local Network" privacy setting may block non-Apple
# binaries such as Homebrew/uv Python from reaching the LAN, while /usr/bin/curl is allowed.
set -euo pipefail
FW="$(cd "$(dirname "$0")/.." && pwd)"
if command -v uvx >/dev/null 2>&1; then PIO=(uvx --python 3.13 --from platformio pio); else PIO=(pio); fi
ini() { grep -E "^$1[[:space:]]*=" "$FW/ota_secrets.ini" | sed -E 's/^[^=]+=[[:space:]]*//; s/[[:space:]]+$//'; }
HOST="${1:-$(ini host)}"
PASS="$(ini password)"
ENV="${PIO_ENV:-twatch}"
BIN="$FW/.pio/build/$ENV/firmware.bin"
CURL="$(command -v /usr/bin/curl || command -v curl)"

[[ "${SKIP_BUILD:-}" == 1 ]] || "${PIO[@]}" run -d "$FW" -e "$ENV"

# mDNS lookups through curl are slow/flaky right after boot: resolve once via Bonjour, then use the IP
if [[ "$HOST" == *.local ]] && command -v dscacheutil >/dev/null 2>&1; then
  IP="$(dscacheutil -q host -a name "$HOST" 2>/dev/null | awk '/ip_address/ {print $2; exit}')"
  [[ -n "$IP" ]] && HOST="$IP"
fi

echo "before: $($CURL -fsS -m 5 "http://$HOST/info" || echo 'watch not reachable (on a charger? same Wi-Fi?)')"
echo "uploading $(( $(stat -f%z "$BIN" 2>/dev/null || stat -c%s "$BIN") / 1024 )) KB to $HOST ..."
$CURL -fsS -m 180 -u "ota:$PASS" -F "firmware=@$BIN" "http://$HOST/update"

# wait for the reboot and show which image is running now
for _ in $(seq 1 30); do
  sleep 2
  if info="$($CURL -fsS -m 3 "http://$HOST/info" 2>/dev/null)"; then echo "after:  $info"; exit 0; fi
done
echo "watch did not come back within 60 s (if it keeps failing it rolls back after 3 boots)" >&2
exit 1
