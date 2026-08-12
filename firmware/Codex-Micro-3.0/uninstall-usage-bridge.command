#!/bin/zsh
set -e

LABEL="com.imliubo.codex-micro-usage"
PLIST_PATH="$HOME/Library/LaunchAgents/$LABEL.plist"
launchctl bootout "gui/$UID/$LABEL" >/dev/null 2>&1 || true
if [[ -f "$PLIST_PATH" ]]; then
  mv "$PLIST_PATH" "$HOME/.Trash/$LABEL.plist"
fi
echo "Codex Micro usage auto-sync has been removed."
