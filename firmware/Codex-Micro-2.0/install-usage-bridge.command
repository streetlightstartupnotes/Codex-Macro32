#!/bin/zsh
set -e

ROOT_DIR="${0:A:h}"
SOURCE_COMPANION_DIR="$ROOT_DIR/companion"
COMPANION_DIR="$HOME/Library/Application Support/CodexMicro"
VENV_DIR="$COMPANION_DIR/.venv"
LABEL="com.imliubo.codex-micro-usage"
AGENT_DIR="$HOME/Library/LaunchAgents"
LOG_DIR="$HOME/Library/Logs/CodexMicro"
PLIST_PATH="$AGENT_DIR/$LABEL.plist"

launchctl bootout "gui/$UID/$LABEL" >/dev/null 2>&1 || true
mkdir -p "$COMPANION_DIR" "$AGENT_DIR" "$LOG_DIR"
cp "$SOURCE_COMPANION_DIR/codex_usage_bridge.py" "$COMPANION_DIR/codex_usage_bridge.py"
cp "$SOURCE_COMPANION_DIR/requirements.txt" "$COMPANION_DIR/requirements.txt"

cd "$COMPANION_DIR"
if [[ ! -x "$VENV_DIR/bin/python" ]] || ! "$VENV_DIR/bin/python" -c 'import bleak, objc' >/dev/null 2>&1; then
  echo "Installing the local Bluetooth runtime..."
  python3 -m venv --clear "$VENV_DIR"
  "$VENV_DIR/bin/python" -m pip install --upgrade pip
  "$VENV_DIR/bin/python" -m pip install -r requirements.txt
fi

cat > "$PLIST_PATH" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$VENV_DIR/bin/python</string>
    <string>$COMPANION_DIR/codex_usage_bridge.py</string>
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>10</integer>
  <key>EnvironmentVariables</key>
  <dict><key>PYTHONUNBUFFERED</key><string>1</string></dict>
  <key>StandardOutPath</key><string>$LOG_DIR/usage-bridge.log</string>
  <key>StandardErrorPath</key><string>$LOG_DIR/usage-bridge-error.log</string>
</dict>
</plist>
EOF

launchctl bootstrap "gui/$UID" "$PLIST_PATH"
launchctl kickstart -k "gui/$UID/$LABEL"

echo "Installed. Codex usage sync now starts automatically at login."
echo "Log: $LOG_DIR/usage-bridge.log"
