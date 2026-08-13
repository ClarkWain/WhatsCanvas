#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if command -v python3 >/dev/null 2>&1; then
    PYTHON=python3
else
    PYTHON=python
fi

"$PYTHON" "$ROOT_DIR/scripts/generate_api_reference.py" --check
echo "API_REFERENCE_CHECK_RESULT=PASS"
