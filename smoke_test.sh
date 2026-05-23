#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR="$ROOT_DIR/build"
LOG_PATH="$BUILD_DIR/smoke_test.log"
EXPECTED_HASH="${1:-}"

sh "$ROOT_DIR/build.sh" --no-run

EXE_PATH="$BUILD_DIR/PrismCanvasDemo"
if [ ! -x "$EXE_PATH" ] && [ -x "$BUILD_DIR/Debug/PrismCanvasDemo" ]; then
    EXE_PATH="$BUILD_DIR/Debug/PrismCanvasDemo"
fi
if [ ! -x "$EXE_PATH" ] && [ -x "$BUILD_DIR/Debug/PrismCanvasDemo.exe" ]; then
    EXE_PATH="$BUILD_DIR/Debug/PrismCanvasDemo.exe"
fi

if [ ! -x "$EXE_PATH" ]; then
    echo "Executable not found: $EXE_PATH"
    exit 1
fi

export CPPDEMO_PRINT_PIXEL_HASH=1
export CPPDEMO_EXIT_AFTER_FIRST_FRAME=1
export CPPDEMO_FIXED_TIME_SECONDS=1.25
export CPPDEMO_DISABLE_MSAA=1
if [ -n "$EXPECTED_HASH" ]; then
    export CPPDEMO_EXPECT_PIXEL_HASH="$EXPECTED_HASH"
fi

"$EXE_PATH" > "$LOG_PATH" 2>&1
RUN_EXIT=$?
cat "$LOG_PATH"
if [ "$RUN_EXIT" -ne 0 ]; then
    exit "$RUN_EXIT"
fi

if ! grep -q "PIXEL_HASH_RGBA=" "$LOG_PATH"; then
    echo "Pixel hash output missing."
    exit 1
fi

if grep -Ei "Pixel hash mismatch|Pixel hash expectation invalid|Pixel readback failed|PPM capture failed|Fixed time invalid|SHADER::COMPILATION_FAILED|PROGRAM_LINKING_ERROR" "$LOG_PATH" >/dev/null; then
    echo "Smoke test found a rendering failure marker."
    exit 1
fi

echo "SMOKE_TEST=PASS"
exit 0
