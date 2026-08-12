#!/bin/zsh
set -eu

PROJECT_DIR="${0:A:h}"
cd "$PROJECT_DIR"

LOCAL_PIO_PYTHON="$PROJECT_DIR/usb-mic/.pio-core/penv/bin/python"
LOCAL_PLATFORMIO="$PROJECT_DIR/usb-mic/.pio-core/penv/bin/platformio"
if [[ -x "$LOCAL_PLATFORMIO" ]]; then
  PIO=("$LOCAL_PLATFORMIO")
elif [[ -x "$LOCAL_PIO_PYTHON" ]] &&
    "$LOCAL_PIO_PYTHON" -m platformio --version >/dev/null 2>&1; then
  PIO=("$LOCAL_PIO_PYTHON" -m platformio)
elif command -v pio >/dev/null 2>&1; then
  PIO=(pio)
elif python3 -m platformio --version >/dev/null 2>&1; then
  PIO=(python3 -m platformio)
else
  echo "PlatformIO is required. Install it with: python3 -m pip install platformio"
  exit 1
fi

REQUESTED_PORT="${1:-}"

echo "Running regression checks..."
./scripts/check-regressions.sh
echo "Building Codex Macro32 V3 with USB microphone support..."
(
  cd usb-mic
  "${PIO[@]}" run
)
APP_BIN="$PROJECT_DIR/usb-mic/.pio/build/waveshare-1_85b-usb-mic/firmware.bin"
ESPTOOL_PACKAGE="$PROJECT_DIR/usb-mic/.pio-core/packages/tool-esptoolpy"
PIO_INFO=$(
  cd "$PROJECT_DIR/usb-mic"
  "${PIO[@]}" system info --json-output
)
PIO_PYTHON=$(python3 -c \
  'import json, sys; print(json.load(sys.stdin)["python_exe"]["value"])' \
  <<< "$PIO_INFO")
if [[ ! -x "$PIO_PYTHON" || ! -d "$ESPTOOL_PACKAGE" ]]; then
  echo "The USB microphone build did not prepare its local flashing tools."
  exit 1
fi
if [[ -n "$REQUESTED_PORT" ]]; then
  PORT_INFO=$("$PIO_PYTHON" "$PROJECT_DIR/scripts/prepare-flash-port.py" "$REQUESTED_PORT")
else
  PORT_INFO=$("$PIO_PYTHON" "$PROJECT_DIR/scripts/prepare-flash-port.py")
fi
PORT="${PORT_INFO%%|*}"
BEFORE="${PORT_INFO##*|}"
echo "Writing only the application partition on $PORT."
echo "NVS, BLE bonds, cached quota and companion settings are preserved."
PYTHONPATH="$ESPTOOL_PACKAGE" "$PIO_PYTHON" -m esptool \
  --chip esp32s3 --port "$PORT" --baud 921600 --before "$BEFORE" \
  --after watchdog-reset \
  write-flash 0x10000 "$APP_BIN"
echo "V3 USB microphone build and application-only upload complete."
