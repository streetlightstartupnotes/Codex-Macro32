#!/bin/zsh
set -eu

cd "${0:A:h}"

if command -v pio >/dev/null 2>&1; then
  PIO=(pio)
elif python3 -m platformio --version >/dev/null 2>&1; then
  PIO=(python3 -m platformio)
else
  echo "PlatformIO is required. Install it with: python3 -m pip install platformio"
  exit 1
fi

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  ports=(/dev/cu.usbmodem*(N) /dev/cu.SLAB_USBtoUART*(N) /dev/cu.wchusbserial*(N))
  if (( ${#ports[@]} != 1 )); then
    echo "Could not select one serial device automatically."
    echo "Usage: ./flash.command /dev/cu.your-device"
    (( ${#ports[@]} > 0 )) && printf 'Detected: %s\n' "${ports[@]}"
    exit 1
  fi
  PORT="${ports[1]}"
fi

echo "Running regression checks..."
./scripts/check-regressions.sh
echo "Building Codex Micro 3.0 Preview from source..."
"${PIO[@]}" run -e waveshare-1_85b
echo "Uploading to $PORT. Hold BOOT if the connection waits."
"${PIO[@]}" run -e waveshare-1_85b -t upload --upload-port "$PORT"
echo "Build and upload complete."
