#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/../../../.." && pwd)
BUILD_DIR=${1:-${REPO_ROOT}/out/wasm-web}
EMSDK_ROOT=${EMSDK_ROOT:-${REPO_ROOT}/out/emsdk}

export EMSDK_ROOT
"${REPO_ROOT}/platforms/wasm/build.sh" "${BUILD_DIR}" SpiderSolitaireWeb

echo "Spider Solitaire Web build: ${BUILD_DIR}/platforms/wasm/web/spider.html"
