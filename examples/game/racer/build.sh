#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR="$ROOT_DIR/build"
CONFIG="Debug"
TARGET="Racer"
NO_RUN=0

for arg in "$@"; do
    case "$arg" in
        --no-run) NO_RUN=1 ;;
        --release) CONFIG="Release" ;;
        --debug) CONFIG="Debug" ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: ./build.sh [--no-run] [--debug|--release]"
            exit 1
            ;;
    esac
done

if ! command -v cmake >/dev/null 2>&1; then
    echo "CMake was not found in PATH."
    exit 1
fi

if [ -d "$ROOT_DIR/.git" ] && command -v git >/dev/null 2>&1; then
    echo "[0/3] Updating submodules..."
    git -C "$ROOT_DIR" submodule update --init --recursive
fi

echo "[1/3] Configuring..."
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "[2/3] Building..."
cmake --build "$BUILD_DIR" --config "$CONFIG" --target "$TARGET"

if [ "$NO_RUN" -eq 1 ]; then
    echo "[3/3] Skipping run."
    exit 0
fi

EXE_PATH="$BUILD_DIR/$TARGET"
if [ ! -x "$EXE_PATH" ] && [ -x "$BUILD_DIR/$CONFIG/$TARGET" ]; then
    EXE_PATH="$BUILD_DIR/$CONFIG/$TARGET"
fi
if [ ! -x "$EXE_PATH" ] && [ -x "$BUILD_DIR/$CONFIG/$TARGET.exe" ]; then
    EXE_PATH="$BUILD_DIR/$CONFIG/$TARGET.exe"
fi

if [ ! -x "$EXE_PATH" ]; then
    echo "Executable not found: $EXE_PATH"
    exit 1
fi

echo "[3/3] Running..."
"$EXE_PATH"