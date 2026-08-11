#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
BUILD_DIR=${WHATSCANVAS_PREFLIGHT_BUILD_DIR:-"$ROOT_DIR/build-preflight"}
CONFIG=${WHATSCANVAS_PREFLIGHT_CONFIG:-Debug}

"$SCRIPT_DIR/api_reference_check.sh"
"$SCRIPT_DIR/version_consistency_check.sh"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$BUILD_DIR" --config "$CONFIG"

ctest --test-dir "$BUILD_DIR" -C "$CONFIG" -L unit --output-on-failure
ctest --test-dir "$BUILD_DIR" -C "$CONFIG" -R "^(WhatsCanvasApiReferenceCheck|WhatsCanvasVersionConsistencyCheck|WhatsCanvasPackageConsumerSmoke)$" --output-on-failure

echo "RELEASE_PREFLIGHT_RESULT=PASS"
