#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "${script_dir}/../.." && pwd)
build_dir=${1:-${repo_root}/out/wasm-web}
port=${2:-8080}
web_root=${build_dir}/platforms/wasm/web

if [ ! -f "${web_root}/index.html" ]; then
    echo "Web build is missing. Run platforms/wasm/build.sh first." >&2
    exit 1
fi

echo "Serving http://127.0.0.1:${port}/"
python3 -m http.server "${port}" --bind 127.0.0.1 --directory "${web_root}"
