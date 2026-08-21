#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
IOS_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)
REPOSITORY_ROOT=$(CDPATH= cd -- "${IOS_ROOT}/../.." && pwd)
OUTPUT="${REPOSITORY_ROOT}/out/visual-parity/captures/ios"
DEVICE=""
BUNDLE_ID="com.whatscanvas.demo"

usage() {
    echo "Usage: $0 [--device SIMULATOR_UDID] [--output DIR]"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --device) DEVICE="$2"; shift 2 ;;
        --output) OUTPUT="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

if [ -z "${DEVICE}" ]; then
    DEVICE=$(xcrun simctl list devices booted \
        | awk -F '[()]' '/Booted/ {print $2; exit}')
fi
if [ -z "${DEVICE}" ]; then
    echo "No booted iOS simulator found; boot one or pass --device." >&2
    exit 1
fi

xcodebuild test \
    -project "${IOS_ROOT}/WhatsCanvasDemo.xcodeproj" \
    -scheme WhatsCanvasDemo \
    -destination "platform=iOS Simulator,id=${DEVICE}" \
    -only-testing:WhatsCanvasDemoUITests/WhatsCanvasDemoUITests/testVisualParityCaptureMatrix \
    CODE_SIGNING_ALLOWED=NO

CONTAINER=$(xcrun simctl get_app_container "${DEVICE}" "${BUNDLE_ID}" data)
for VIEWPORT in portrait landscape; do
    for SCENE in feature_showcase text_stress geometry_stress compositing_stress; do
        DESTINATION="${OUTPUT}/${SCENE}/${VIEWPORT}"
        mkdir -p "${DESTINATION}"
        if [ "${SCENE}" = feature_showcase ]; then
            SAMPLE_IDS="t0000 t0500 t1250 t2000"
        else
            SAMPLE_IDS="t1250"
        fi
      for SAMPLE_ID in ${SAMPLE_IDS}; do
        STEM="${SCENE}-${VIEWPORT}-${SAMPLE_ID}"
        for SUFFIX in png json; do
            SOURCE="${CONTAINER}/Documents/${STEM}.${SUFFIX}"
            if [ ! -f "${SOURCE}" ]; then
                echo "Missing simulator capture: ${SOURCE}" >&2
                exit 1
            fi
            cp "${SOURCE}" "${DESTINATION}/${SAMPLE_ID}.${SUFFIX}"
        done
        echo "Captured ios/${VIEWPORT}/${SAMPLE_ID}"
      done
    done
done

echo "iOS visual-parity captures: ${OUTPUT}"
