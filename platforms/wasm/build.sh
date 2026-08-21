#!/bin/sh
set -eu

emsdk_version=4.0.22
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "${script_dir}/../.." && pwd)
data_root=${XDG_DATA_HOME:-${HOME}/.local/share}
emsdk_root=${EMSDK_ROOT:-${data_root}/emsdk}
build_dir=${1:-${repo_root}/out/wasm-web}

if ! command -v emcc >/dev/null 2>&1; then
    if [ ! -f "${emsdk_root}/emsdk_env.sh" ]; then
        echo "Emscripten is missing. Run platforms/wasm/bootstrap.sh first." >&2
        exit 1
    fi
    # shellcheck disable=SC1090
    . "${emsdk_root}/emsdk_env.sh" >/dev/null
fi

actual_version=$(emcc --version | sed -n '1s/[^0-9]*\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\).*/\1/p')
case "${actual_version}" in
    ${emsdk_version}*) ;;
    *)
        echo "Expected Emscripten ${emsdk_version}, found ${actual_version:-unknown}." >&2
        exit 1
        ;;
esac

emcmake cmake -S "${repo_root}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DWHATSCANVAS_BUILD_BENCHMARKS=OFF \
    -DWHATSCANVAS_BUILD_DEMO=OFF \
    -DWHATSCANVAS_BUILD_DESKTOP_PLATFORM=OFF \
    -DWHATSCANVAS_BUILD_METAL=OFF \
    -DWHATSCANVAS_BUILD_OPENGL=OFF \
    -DWHATSCANVAS_BUILD_OPENGLES=ON \
    -DWHATSCANVAS_BUILD_SOFTWARE=OFF \
    -DWHATSCANVAS_BUILD_WASM_WEB=ON \
    -DWHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=ON \
    -DWHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON \
    -DWHATSCANVAS_INSTALL=OFF \
    -DWHATSCANVAS_X11=OFF
cmake --build "${build_dir}" --target WhatsCanvasWeb --parallel

echo "Web build: ${build_dir}/platforms/wasm/web/index.html"
