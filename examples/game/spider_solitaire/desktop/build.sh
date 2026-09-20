#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR="$ROOT_DIR/build"
CONFIG="Release"
NO_RUN=0

for arg in "$@"; do
    case "$arg" in
        --no-run) NO_RUN=1 ;;
        --release) CONFIG="Release" ;;
        --debug) CONFIG="Debug" ;;
        *) echo "Usage: ./build.sh [--no-run] [--debug|--release]"; exit 1 ;;
    esac
done

# Drop a CMakeCache.txt whose CMAKE_HOME_DIRECTORY points at a different
# checkout. Prevents 'source ... does not match ... used to generate cache'
# when the repo is moved between paths.
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    cached=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt" | head -n 1 || true)
    if [ -n "$cached" ] && [ "$cached" != "$ROOT_DIR" ]; then
        echo "Stale CMakeCache.txt (points at $cached); wiping."
        rm -rf "$BUILD_DIR"
    fi
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$BUILD_DIR" --config "$CONFIG" --target SpiderSolitaire

if [ "$NO_RUN" -eq 0 ]; then
    EXE="$BUILD_DIR/SpiderSolitaire"
    [ -x "$EXE" ] || EXE="$BUILD_DIR/$CONFIG/SpiderSolitaire"
    "$EXE"
fi
