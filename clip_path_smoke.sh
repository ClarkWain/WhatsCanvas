#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
EXPECTED_HASH="${1:-}"

export CPPDEMO_EXERCISE_CLIP_PATH=1
if [ -n "$EXPECTED_HASH" ]; then
    sh "$ROOT_DIR/smoke_test.sh" "$EXPECTED_HASH"
else
    sh "$ROOT_DIR/smoke_test.sh"
fi

echo "CLIP_PATH_SMOKE_TEST=PASS"
echo "CLIP_PATH_SMOKE_RESULT=PASS"