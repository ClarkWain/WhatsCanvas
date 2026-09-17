#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CONFIG=Release
NORUN=0
for arg in "$@"; do
    case "$arg" in
        --debug) CONFIG=Debug ;;
        --release) CONFIG=Release ;;
        --no-run) NORUN=1 ;;
        *) echo "Usage: build.sh [--no-run] [--debug|--release]"; exit 1 ;;
    esac
done
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$ROOT/build" --config "$CONFIG" --parallel
[ "$NORUN" -eq 1 ] && exit 0
EXE="$ROOT/build/Xiangqi"
[ -x "$EXE" ] || EXE="$ROOT/build/$CONFIG/Xiangqi"
exec "$EXE"
