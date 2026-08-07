#!/bin/zsh
set -e

cd "${0:A:h}/companion"
if [[ ! -x .venv/bin/python ]] || ! .venv/bin/python -c 'import bleak, objc' >/dev/null 2>&1; then
  echo "Repairing the local Python environment..."
  python3 -m venv --clear .venv
  .venv/bin/python -m pip install --upgrade pip
  .venv/bin/python -m pip install -r requirements.txt
fi

echo "Keep this window open to sync the weekly Codex allowance."
echo "Bluetooth must be enabled and Codex Micro must be paired."
exec .venv/bin/python codex_usage_bridge.py
