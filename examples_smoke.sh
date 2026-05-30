#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
START_TS=$(date +%s)

echo "EXAMPLES_SMOKE_TARGETS=Tetris,Racer,BubbleShooter"

run_example() {
    name="$1"
    script="$2"

    if [ ! -f "$script" ]; then
        echo "Example build script not found: $script"
        echo "EXAMPLES_SMOKE_RESULT=FAIL"
        echo "EXAMPLES_SMOKE_FAILED_TARGET=$name"
        echo "EXAMPLES_SMOKE_FAILED_STAGE=SCRIPT_MISSING"
        exit 1
    fi

    echo "[EXAMPLE] $name"
    step_start=$(date +%s)
    sh "$script" --no-run
    step_end=$(date +%s)
    step_ms=$(( (step_end - step_start) * 1000 ))
    echo "EXAMPLES_SMOKE_${name}_MS=$step_ms"
}

run_example Tetris "$ROOT_DIR/example/game/tetris/build.sh"
run_example Racer "$ROOT_DIR/example/game/racer/build.sh"
run_example BubbleShooter "$ROOT_DIR/example/game/bubble_shooter/build.sh"

END_TS=$(date +%s)
TOTAL_MS=$(( (END_TS - START_TS) * 1000 ))
echo "EXAMPLES_SMOKE_TOTAL_MS=$TOTAL_MS"
echo "EXAMPLES_SMOKE_RESULT=PASS"