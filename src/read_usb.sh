#!/bin/bash
#
# read_usb.sh
# ----------
# Wrapper for RTT capture using src/read_usb.py (pynrfjprog backend).
#
# Env vars:
# - CSV_FILE:           output CSV file path/name
# - AUTO_RESET=0|1:     reset target before attach (default: 0)
# - STARTUP_DELAY_SEC:  wait after reset/attach before RTT read (default: 0.25)
# - JLINK_SN / NRF_SN:  optional explicit probe serial number
#
# Optional:
# - PYTHON_BIN:         python interpreter to use

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -n "${PYTHON_BIN:-}" ]; then
    PY="${PYTHON_BIN}"
elif [ -x "${SCRIPT_DIR}/../venv/bin/python" ]; then
    PY="${SCRIPT_DIR}/../venv/bin/python"
else
    PY="$(command -v python3)"
fi

if ! "$PY" -c 'import pynrfjprog' >/dev/null 2>&1; then
    echo "Installing missing Python dependency: pynrfjprog"
    "$PY" -m pip install -q pynrfjprog
fi

exec "$PY" -u "${SCRIPT_DIR}/read_usb.py"
