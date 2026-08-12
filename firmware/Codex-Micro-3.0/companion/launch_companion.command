#!/bin/zsh
set -eu

companion_dir="${0:A:h}"
cd "$companion_dir"

if [[ ! -x .venv/bin/python ]]; then
  python3 -m venv .venv
fi

if ! .venv/bin/python -c 'import bleak, CoreBluetooth' >/dev/null 2>&1; then
  .venv/bin/python -m pip install -r requirements.txt
fi

exec .venv/bin/python companion_gui.py
