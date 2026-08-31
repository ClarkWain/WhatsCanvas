#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/../../../.." && pwd)
BUILD_DIR=${1:-${REPO_ROOT}/out/wasm-web}
PORT=${2:-8081}
WEB_ROOT="${BUILD_DIR}/platforms/wasm/web"

if [ ! -f "${WEB_ROOT}/spider.html" ]; then
    echo "Spider Web build is missing. Run ./build.sh first." >&2
    exit 1
fi

echo "Serving http://127.0.0.1:${PORT}/spider.html"
python3 -m http.server "${PORT}" --bind 127.0.0.1 --directory "${WEB_ROOT}"
