#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEFAULT_HASH="${1:-9862191092520609134}"
CLIP_HASH="${2:-10630383327616444847}"

sh "$ROOT_DIR/scripts/smoke_test.sh" "$DEFAULT_HASH"
sh "$ROOT_DIR/scripts/clip_path_smoke.sh" "$CLIP_HASH"

echo "REGRESSION_SMOKE_TEST=PASS"
echo "REGRESSION_SMOKE_RESULT=PASS"
