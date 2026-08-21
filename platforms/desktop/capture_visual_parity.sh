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

capture_one() {
    SCENE="$1"
    VIEWPORT="$2"
    WIDTH="$3"
    HEIGHT="$4"
    SAMPLE_ID="$5"
    SAMPLE_TIME="$6"
    DESTINATION="${OUTPUT}/${SCENE}/${VIEWPORT}"
    mkdir -p "${DESTINATION}"
    "${HOST}" --backend=software --scene="${SCENE}" \
        --w="${WIDTH}" --h="${HEIGHT}" --dpr=3 --time="${SAMPLE_TIME}" \
        --dump-png="${DESTINATION}/${SAMPLE_ID}.ppm"
}

for VIEWPORT_SPEC in "portrait 400 800" "landscape 800 400"; do
    set -- ${VIEWPORT_SPEC}
    VIEWPORT="$1"; WIDTH="$2"; HEIGHT="$3"
    for SAMPLE in "t0000 0.0" "t0500 0.5" "t1250 1.25" "t2000 2.0"; do
        set -- ${SAMPLE}
        capture_one feature_showcase "${VIEWPORT}" "${WIDTH}" "${HEIGHT}" "$1" "$2"
    done
    for SCENE in text_stress geometry_stress compositing_stress; do
        capture_one "${SCENE}" "${VIEWPORT}" "${WIDTH}" "${HEIGHT}" t1250 1.25
    done
done
echo "Desktop visual-parity captures: ${OUTPUT}"
