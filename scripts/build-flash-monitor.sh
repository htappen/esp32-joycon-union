#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE_DIR="$PROJECT_DIR/firmware"
IDF_ROOT="${IDF_PATH:-/home/htappen/esp/esp-idf}"
PORT="${1:-/dev/ttyACM0}"
DURATION="${2:-15}"

if [[ ! -f "$IDF_ROOT/export.sh" ]]; then
    echo "ESP-IDF not found at $IDF_ROOT" >&2
    echo "Run scripts/install-dependencies.sh or set IDF_PATH." >&2
    exit 1
fi

echo "==> Building firmware"
# shellcheck disable=SC1091
source "$IDF_ROOT/export.sh" >/dev/null
(cd "$FIRMWARE_DIR" && idf.py build)

echo "==> Flashing $PORT"
(cd "$FIRMWARE_DIR" && idf.py -p "$PORT" flash)

echo "==> Capturing serial output for ${DURATION}s"
python3 - "$PORT" "$DURATION" <<'PY'
import sys
import time

import serial

port = sys.argv[1]
duration = float(sys.argv[2])

with serial.Serial(port, 115200, timeout=0.2) as device:
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        data = device.read(4096)
        if data:
            sys.stdout.write(data.decode("utf-8", errors="replace"))
            sys.stdout.flush()
PY
