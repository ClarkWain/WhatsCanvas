#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT/build"}
OUTPUT_DIR=${OUTPUT_DIR:-"$BUILD_DIR/performance-results"}
PROFILE=${PROFILE:-standard}
BACKENDS=${BACKENDS:-software}
SCENES=${SCENES:-all}

if [ -x "$BUILD_DIR/Release/WhatsCanvasPerformanceSuite" ]; then
    SUITE="$BUILD_DIR/Release/WhatsCanvasPerformanceSuite"
elif [ -x "$BUILD_DIR/WhatsCanvasPerformanceSuite" ]; then
    SUITE="$BUILD_DIR/WhatsCanvasPerformanceSuite"
else
    echo "WhatsCanvasPerformanceSuite Release executable was not found in $BUILD_DIR" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
WHATSCANVAS_PERF_COMMIT=$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || true)
export WHATSCANVAS_PERF_COMMIT

if [ "$SCENES" = "all" ]; then
    SCENES=$("$SUITE" --list-scenes | awk '{print $1}')
fi

for backend in $BACKENDS; do
    for scene in $SCENES; do
        echo "Running $backend/$scene ($PROFILE)..."
        result="$OUTPUT_DIR/$backend-$scene.jsonl"
        "$SUITE" \
            --backend "$backend" \
            --profile "$PROFILE" \
            --scene "$scene" \
            --output "$result"
        python3 "$ROOT/scripts/compare_performance.py" --validate "$result"
    done
done

echo "Results: $OUTPUT_DIR"
