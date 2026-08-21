#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPOSITORY_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/../.." && pwd)
OUTPUT="${REPOSITORY_ROOT}/out/visual-parity/captures/desktop"
HOST=""

usage() {
    echo "Usage: $0 [--host WhatsCanvasDesktopHost] [--output DIR]"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --host) HOST="$2"; shift 2 ;;
        --output) OUTPUT="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

if [ -z "${HOST}" ]; then
    for CANDIDATE in \
        "${REPOSITORY_ROOT}/build/WhatsCanvasDesktopHost" \
        "${REPOSITORY_ROOT}/build/Release/WhatsCanvasDesktopHost" \
        "${REPOSITORY_ROOT}/out/native-web-change-smoke/Release/WhatsCanvasDesktopHost"; do
        if [ -x "${CANDIDATE}" ]; then
            HOST="${CANDIDATE}"
            break
        fi
    done
fi
if [ -z "${HOST}" ] || [ ! -x "${HOST}" ]; then
    echo "WhatsCanvasDesktopHost was not found; build it or pass --host." >&2
    exit 1
fi

capture_viewport() {
    VIEWPORT="$1"
    WIDTH="$2"
    HEIGHT="$3"
    DESTINATION="${OUTPUT}/feature_showcase/${VIEWPORT}"
    mkdir -p "${DESTINATION}"
    for SAMPLE in "t0000 0.0" "t0500 0.5" "t1250 1.25" "t2000 2.0"; do
        set -- ${SAMPLE}
        "${HOST}" --backend=software --scene=feature_showcase \
            --w="${WIDTH}" --h="${HEIGHT}" --dpr=3 --time="$2" \
            --dump-png="${DESTINATION}/$1.ppm"
    done
}

capture_viewport portrait 393 759
capture_viewport landscape 786 377
echo "Desktop visual-parity captures: ${OUTPUT}"
