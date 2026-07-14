#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR="$ROOT_DIR/build"
CONFIG="Debug"
TARGET="WhatsCanvasDemo"
NO_RUN=0
PACKAGE=0

for arg in "$@"; do
    case "$arg" in
        --no-run) NO_RUN=1 ;;
        --release) CONFIG="Release" ;;
        --debug) CONFIG="Debug" ;;
        --package) PACKAGE=1 ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: ./build.sh [--no-run] [--debug|--release] [--package]"
            exit 1
            ;;
    esac
done

PACKAGE_DIR="$ROOT_DIR/out/package/$CONFIG"

if ! command -v cmake >/dev/null 2>&1; then
    echo "CMake was not found in PATH."
    exit 1
fi

if [ -d "$ROOT_DIR/.git" ] && command -v git >/dev/null 2>&1; then
    echo "[0/3] Updating submodules..."
    git -C "$ROOT_DIR" submodule update --init --recursive
fi

echo "[1/3] Configuring..."
PACKAGE_CMAKE_ARGS=""
if [ "$PACKAGE" -eq 1 ] && [ "${WHATSCANVAS_PACKAGE_ENABLE_FREETYPE:-0}" != "1" ]; then
    PACKAGE_CMAKE_ARGS="$PACKAGE_CMAKE_ARGS -DWHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=OFF"
fi
if [ "$PACKAGE" -eq 1 ]; then
    # Packaging installs the renderer libraries, not the demo, so skip building
    # the test targets to keep the package build lean.
    PACKAGE_CMAKE_ARGS="$PACKAGE_CMAKE_ARGS -DBUILD_TESTING=OFF"
fi
# shellcheck disable=SC2086
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $PACKAGE_CMAKE_ARGS ${WHATSCANVAS_CMAKE_EXTRA_ARGS:-}

echo "[2/3] Building..."
if [ "$PACKAGE" -eq 1 ]; then
    # cmake --install exports every enabled renderer library (OpenGL and the
    # dependency-free Software target). The demo only links OpenGL, so building
    # just "$TARGET" would leave WhatsCanvasSoftware unbuilt and the install
    # step would fail. Build the default target set so all installed libraries
    # exist before packaging.
    cmake --build "$BUILD_DIR" --config "$CONFIG"
else
    cmake --build "$BUILD_DIR" --config "$CONFIG" --target "$TARGET"
fi

if [ "$PACKAGE" -eq 1 ]; then
    echo "[3/4] Packaging..."
    rm -rf "$PACKAGE_DIR"
    cmake --install "$BUILD_DIR" --config "$CONFIG" --prefix "$PACKAGE_DIR"
    echo "BUILD_PACKAGE_DIR=$PACKAGE_DIR"
fi

if [ "$NO_RUN" -eq 1 ]; then
    if [ "$PACKAGE" -eq 1 ]; then
        echo "[4/4] Skipping run."
    else
        echo "[3/3] Skipping run."
    fi
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

if [ "$PACKAGE" -eq 1 ]; then
    echo "[4/4] Running..."
else
    echo "[3/3] Running..."
fi
"$EXE_PATH"
