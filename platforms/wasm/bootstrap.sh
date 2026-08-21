#!/bin/sh
set -eu

emsdk_version=4.0.22
data_root=${XDG_DATA_HOME:-${HOME}/.local/share}
emsdk_root=${EMSDK_ROOT:-${data_root}/emsdk}

if command -v python3 >/dev/null 2>&1; then
    python_bin=$(command -v python3)
elif [ -x /opt/homebrew/opt/python@3.13/libexec/bin/python3 ]; then
    python_bin=/opt/homebrew/opt/python@3.13/libexec/bin/python3
else
    echo "Python 3.10 or newer is required by emsdk." >&2
    exit 1
fi

if ! "${python_bin}" -c 'import sys; raise SystemExit(sys.version_info < (3, 10))'; then
    if [ -x /opt/homebrew/opt/python@3.13/libexec/bin/python3 ]; then
        python_bin=/opt/homebrew/opt/python@3.13/libexec/bin/python3
    else
        echo "Python 3.10 or newer is required by emsdk." >&2
        exit 1
    fi
fi

python_dir=$(dirname "${python_bin}")
PATH="${python_dir}:${PATH}"
export PATH

if [ ! -d "${emsdk_root}/.git" ]; then
    mkdir -p "$(dirname "${emsdk_root}")"
    git clone https://github.com/emscripten-core/emsdk.git "${emsdk_root}"
else
    git -C "${emsdk_root}" fetch --tags --prune
fi

"${emsdk_root}/emsdk" install "${emsdk_version}"
"${emsdk_root}/emsdk" activate "${emsdk_version}"

echo "Emscripten ${emsdk_version} is installed in ${emsdk_root}."
echo "Run: . ${emsdk_root}/emsdk_env.sh"
