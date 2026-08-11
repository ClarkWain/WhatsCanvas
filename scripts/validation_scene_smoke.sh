#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR="$ROOT_DIR/build"
SCENES="text-heavy text-showcase image-heavy gradient-effect clipping transform save-layer"

sh "$ROOT_DIR/build.sh" --no-run

EXE_PATH="$BUILD_DIR/WhatsCanvasDemo"
if [ ! -x "$EXE_PATH" ] && [ -x "$BUILD_DIR/Debug/WhatsCanvasDemo" ]; then
    EXE_PATH="$BUILD_DIR/Debug/WhatsCanvasDemo"
fi
if [ ! -x "$EXE_PATH" ] && [ -x "$BUILD_DIR/Debug/WhatsCanvasDemo.exe" ]; then
    EXE_PATH="$BUILD_DIR/Debug/WhatsCanvasDemo.exe"
fi

if [ ! -x "$EXE_PATH" ]; then
    echo "Executable not found: $EXE_PATH"
    exit 1
fi

for scene in $SCENES; do
    log_path="$BUILD_DIR/validation_scene_${scene}.log"
    echo "VALIDATION_SCENE=$scene"
    WHATSCANVAS_VALIDATION_SCENE="$scene" \
    WHATSCANVAS_PRINT_PIXEL_HASH=1 \
    WHATSCANVAS_EXIT_AFTER_FIRST_FRAME=1 \
    WHATSCANVAS_FIXED_TIME_SECONDS=1.25 \
    WHATSCANVAS_DISABLE_MSAA=1 \
        "$EXE_PATH" > "$log_path" 2>&1
    run_exit=$?
    cat "$log_path"
    if [ "$run_exit" -ne 0 ]; then
        echo "VALIDATION_SCENE_SMOKE_FAILED_STAGE=RUN"
        echo "VALIDATION_SCENE_SMOKE_FAILED_SCENE=$scene"
        exit "$run_exit"
    fi

    if ! grep -q "PIXEL_HASH_RGBA=" "$log_path"; then
        echo "Pixel hash output missing."
        echo "VALIDATION_SCENE_SMOKE_FAILED_STAGE=HASH_OUTPUT"
        echo "VALIDATION_SCENE_SMOKE_FAILED_SCENE=$scene"
        exit 1
    fi

    if grep -Ei "Pixel hash mismatch|Pixel hash expectation invalid|Pixel readback failed|PPM capture failed|Fixed time invalid|SHADER::COMPILATION_FAILED|PROGRAM_LINKING_ERROR" "$log_path" >/dev/null; then
        echo "Validation scene smoke found a rendering failure marker."
        echo "VALIDATION_SCENE_SMOKE_FAILED_STAGE=MARKER_SCAN"
        echo "VALIDATION_SCENE_SMOKE_FAILED_SCENE=$scene"
        exit 1
    fi
done

echo "VALIDATION_SCENE_SMOKE_TEST=PASS"
echo "VALIDATION_SCENE_SMOKE_RESULT=PASS"
exit 0
