#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEFAULT_HASH="${1:-2808591959994190122}"
CLIP_HASH="${2:-7173893163224947210}"

sh "$ROOT_DIR/scripts/smoke_test.sh" "$DEFAULT_HASH"
sh "$ROOT_DIR/scripts/clip_path_smoke.sh" "$CLIP_HASH"

echo "REGRESSION_SMOKE_TEST=PASS"
echo "REGRESSION_SMOKE_RESULT=PASS"
