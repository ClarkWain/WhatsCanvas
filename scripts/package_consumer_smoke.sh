#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CONFIG=${WHATSCANVAS_PACKAGE_CONFIG:-Release}
PACKAGE_DIR=${WHATSCANVAS_PACKAGE_DIR:-"$ROOT_DIR/out/package/$CONFIG"}
CONSUMER_BUILD_DIR=${WHATSCANVAS_CONSUMER_BUILD_DIR:-"$ROOT_DIR/build-package-consumer"}
SOFTWARE_CONSUMER_BUILD_DIR="$CONSUMER_BUILD_DIR-software"

if [ ! -f "$PACKAGE_DIR/lib/cmake/WhatsCanvas/WhatsCanvasConfig.cmake" ]; then
    echo "Package not found, building package first: $PACKAGE_DIR"
    sh "$ROOT_DIR/build.sh" --release --package --no-run
fi

rm -rf "$CONSUMER_BUILD_DIR"
rm -rf "$SOFTWARE_CONSUMER_BUILD_DIR"
cmake -S "$ROOT_DIR/tests/package_consumer" \
    -B "$CONSUMER_BUILD_DIR" \
    -DCMAKE_PREFIX_PATH="$PACKAGE_DIR" \
    -DWHATSCANVAS_PACKAGE_TARGET=OpenGL \
    -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$CONSUMER_BUILD_DIR" --config "$CONFIG"

cmake -S "$ROOT_DIR/tests/package_consumer" \
    -B "$SOFTWARE_CONSUMER_BUILD_DIR" \
    -DCMAKE_PREFIX_PATH="$PACKAGE_DIR" \
    -DWHATSCANVAS_PACKAGE_TARGET=Software \
    -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$SOFTWARE_CONSUMER_BUILD_DIR" --config "$CONFIG"

echo "PACKAGE_CONSUMER_SMOKE_RESULT=PASS"
