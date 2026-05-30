#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEFAULT_HASH="${1:-2458027664413625913}"
CLIP_HASH="${2:-12248791335057056593}"

sh "$ROOT_DIR/smoke_test.sh" "$DEFAULT_HASH"
sh "$ROOT_DIR/clip_path_smoke.sh" "$CLIP_HASH"

echo "REGRESSION_SMOKE_TEST=PASS"
echo "REGRESSION_SMOKE_RESULT=PASS"