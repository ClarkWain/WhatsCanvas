#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "${script_dir}/../.." && pwd)
data_root=${XDG_DATA_HOME:-${HOME}/.local/share}
emsdk_root=${EMSDK_ROOT:-${data_root}/emsdk}

if ! command -v node >/dev/null 2>&1; then
    if [ ! -f "${emsdk_root}/emsdk_env.sh" ]; then
        echo "Node.js is missing. Run platforms/wasm/bootstrap.sh first." >&2
        exit 1
    fi
    # shellcheck disable=SC1090
    . "${emsdk_root}/emsdk_env.sh" >/dev/null
fi

WHATSCANVAS_WEB_BUILD_DIR=${WHATSCANVAS_WEB_BUILD_DIR:-${repo_root}/out/wasm-web/platforms/wasm/web}
export WHATSCANVAS_WEB_BUILD_DIR
node "${script_dir}/test_browser.mjs"
