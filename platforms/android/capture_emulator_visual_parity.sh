#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPOSITORY_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/../.." && pwd)
APK="${SCRIPT_DIR}/app/build/outputs/apk/debug/app-debug.apk"
OUTPUT="${REPOSITORY_ROOT}/out/visual-parity/captures/android"
DEVICE=""
PACKAGE="com.whatscanvas.demo"
SCENE_FILTER=""

usage() {
    echo "Usage: $0 [--device SERIAL] [--apk PATH] [--output DIR] [--scene ID]"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --device) DEVICE="$2"; shift 2 ;;
        --apk) APK="$2"; shift 2 ;;
        --output) OUTPUT="$2"; shift 2 ;;
        --scene) SCENE_FILTER="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

if [ -n "${ANDROID_HOME:-}" ] && [ -x "${ANDROID_HOME}/platform-tools/adb" ]; then
    ADB="${ANDROID_HOME}/platform-tools/adb"
else
    ADB=$(command -v adb || true)
fi
if [ -z "${ADB}" ]; then
    echo "adb was not found; set ANDROID_HOME or add platform-tools to PATH." >&2
    exit 1
fi
if [ ! -f "${APK}" ]; then
    echo "APK not found: ${APK}" >&2
    exit 1
fi

adb_cmd() {
    if [ -n "${DEVICE}" ]; then
        "${ADB}" -s "${DEVICE}" "$@"
    else
        "${ADB}" "$@"
    fi
}

display_rotation() {
    adb_cmd shell dumpsys window displays \
        | sed -n 's/.*mDisplayRotation=ROTATION_\([0-9]*\).*/\1/p' \
        | head -n 1
}

rotate_display() {
    EXPECTED_ROTATION="$1"
    case "${EXPECTED_ROTATION}" in
        0) ROTATION_INDEX=0 ;;
        90) ROTATION_INDEX=1 ;;
        180) ROTATION_INDEX=2 ;;
        270) ROTATION_INDEX=3 ;;
        *) echo "Unsupported display rotation: ${EXPECTED_ROTATION}" >&2; exit 1 ;;
    esac
    adb_cmd shell cmd window user-rotation lock "${ROTATION_INDEX}"
    ATTEMPT=0
    ACTIVE_ROTATION=$(display_rotation)
    while [ "${ACTIVE_ROTATION}" != "${EXPECTED_ROTATION}" ] \
        && [ "${ATTEMPT}" -lt 20 ]; do
        ATTEMPT=$((ATTEMPT + 1))
        sleep 0.2
        ACTIVE_ROTATION=$(display_rotation)
    done
    if [ "${ACTIVE_ROTATION}" != "${EXPECTED_ROTATION}" ]; then
        echo "Emulator did not rotate to ${EXPECTED_ROTATION} degrees." >&2
        exit 1
    fi
}

ORIGINAL_WINDOW_ROTATION=$(adb_cmd shell cmd window user-rotation | tr -d '\r')
ORIGINAL_DISPLAY_ROTATION=$(display_rotation)

cleanup() {
    rotate_display "${ORIGINAL_DISPLAY_ROTATION}" >/dev/null 2>&1 || true
    set -- ${ORIGINAL_WINDOW_ROTATION}
    adb_cmd shell cmd window user-rotation "$@" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

adb_cmd install -r "${APK}" >/dev/null

capture_one() {
    SCENE="$1"
    VIEWPORT="$2"
    ROTATION="$3"
    SAMPLE_ID="$4"
    SAMPLE_TIME="$5"
    DESTINATION="${OUTPUT}/${SCENE}/${VIEWPORT}"
    IMAGE="${DESTINATION}/${SAMPLE_ID}.png"
    METADATA="${DESTINATION}/${SAMPLE_ID}.json"

    mkdir -p "${DESTINATION}"
    if [ "${ROTATION}" -eq 1 ]; then
        EXPECTED_ROTATION=90
    else
        EXPECTED_ROTATION=0
    fi
    rotate_display "${EXPECTED_ROTATION}"
    adb_cmd shell cmd window user-rotation lock "${ROTATION}"
    # Give SurfaceFlinger one short turn after locking the chosen orientation.
    sleep 1
    adb_cmd shell am force-stop "${PACKAGE}"
    adb_cmd logcat -c
    # NEW_TASK | CLEAR_TASK prevents a retained task from restoring its prior
    # orientation after an in-place APK install.
    adb_cmd shell am start -W -f 0x10008000 -n "${PACKAGE}/.MainActivity" \
        --ef capture_time_seconds "${SAMPLE_TIME}" \
        --es capture_scene_id "${SCENE}" >/dev/null

    ATTEMPT=0
    READY=""
    ORIENTATION_RETRY=0
    while [ "${ATTEMPT}" -lt 100 ]; do
        READY=$(adb_cmd logcat -d -s WhatsCanvas:I '*:S' \
            | sed -n 's/.*renderer ready: \([0-9]*\)x\([0-9]*\), dpr=\([0-9.]*\).*/\1 \2 \3/p' \
            | tail -n 1)
        if [ -n "${READY}" ]; then
            set -- ${READY}
            if { [ "${VIEWPORT}" = "portrait" ] && [ "$1" -lt "$2" ]; } \
                || { [ "${VIEWPORT}" = "landscape" ] && [ "$1" -gt "$2" ]; }; then
                break
            fi
            if [ "${ORIENTATION_RETRY}" -eq 0 ]; then
                # Some emulator images restore the previous task orientation
                # on the first frame after `adb install -r`. Correct it while
                # the Activity is foreground, then wait for its resized GL
                # surface instead of accepting the stale frame.
                rotate_display "${EXPECTED_ROTATION}"
                adb_cmd shell cmd window user-rotation lock "${ROTATION}"
                ORIENTATION_RETRY=1
            fi
            READY=""
        fi
        ATTEMPT=$((ATTEMPT + 1))
        sleep 0.1
    done
    if [ -z "${READY}" ]; then
        echo "Renderer did not become ready for ${VIEWPORT}/${SAMPLE_ID}." >&2
        exit 1
    fi
    set -- ${READY}
    SURFACE_WIDTH="$1"
    SURFACE_HEIGHT="$2"
    DPR="$3"

    # Canvas/context initialization can complete while Android's cold-start
    # splash is still visible, especially for a text-heavy first frame. Wait
    # for the native renderer to finish that frame before taking a screenshot.
    ATTEMPT=0
    FIRST_FRAME=""
    while [ "${ATTEMPT}" -lt 100 ]; do
        FIRST_FRAME=$(adb_cmd logcat -d -s WhatsCanvas:I '*:S' \
            | sed -n "s/.*First frame ready: scene=${SCENE} size=${SURFACE_WIDTH}x${SURFACE_HEIGHT}.*/ready/p" \
            | tail -n 1)
        if [ -n "${FIRST_FRAME}" ]; then
            break
        fi
        ATTEMPT=$((ATTEMPT + 1))
        sleep 0.1
    done
    if [ -z "${FIRST_FRAME}" ]; then
        echo "First frame did not complete for ${SCENE}/${VIEWPORT}/${SAMPLE_ID}." >&2
        exit 1
    fi
    sleep 0.5
    adb_cmd exec-out screencap -p > "${IMAGE}"
    python3 - "${IMAGE}" "${METADATA}" "${SCENE}" "${VIEWPORT}" \
        "${SAMPLE_ID}" "${SAMPLE_TIME}" "${SURFACE_WIDTH}" \
        "${SURFACE_HEIGHT}" "${DPR}" <<'PY'
import json
import math
import pathlib
import struct
import sys

image_path = pathlib.Path(sys.argv[1])
metadata_path = pathlib.Path(sys.argv[2])
scene_id, viewport_id, sample_id = sys.argv[3], sys.argv[4], sys.argv[5]
sample_time = float(sys.argv[6])
surface_width, surface_height = int(sys.argv[7]), int(sys.argv[8])
dpr = float(sys.argv[9])
data = image_path.read_bytes()
if data[:8] != b"\x89PNG\r\n\x1a\n":
    raise SystemExit(f"invalid screenshot PNG: {image_path}")
screen_width, screen_height = struct.unpack(">II", data[16:24])
if surface_width > screen_width or surface_height > screen_height:
    raise SystemExit("reported surface is outside the screenshot")
canonical_width, canonical_height = (
    (800.0, 400.0) if viewport_id == "landscape" else (400.0, 800.0)
)
scale = min(surface_width / canonical_width, surface_height / canonical_height)
offset_x = (surface_width - canonical_width * scale) * 0.5
round_half_up = lambda value: int(math.floor(value + 0.5))
metadata = {
    "schema_version": 1,
    "scene_id": scene_id,
    "viewport_id": viewport_id,
    "sample_id": sample_id,
    "platform": "android",
    "backend": "opengles",
    "elapsed_seconds": sample_time,
    "device_pixel_ratio": dpr,
    "content_rect_pixels": [
        round_half_up(offset_x),
        0,
        round_half_up(canonical_width * scale),
        round_half_up(canonical_height * scale),
    ],
}
metadata_path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
PY
    echo "Captured android/${VIEWPORT}/${SAMPLE_ID} (${SURFACE_WIDTH}x${SURFACE_HEIGHT}, ${DPR}x)"
}

for VIEWPORT_SPEC in "portrait 0" "landscape 1"; do
    set -- ${VIEWPORT_SPEC}
    VIEWPORT="$1"; ROTATION="$2"
    if [ -z "${SCENE_FILTER}" ] || [ "${SCENE_FILTER}" = feature_showcase ]; then
        for SAMPLE in "t0000 0.0" "t0500 0.5" "t1250 1.25" "t2000 2.0"; do
            set -- ${SAMPLE}
            capture_one feature_showcase "${VIEWPORT}" "${ROTATION}" "$1" "$2"
        done
    fi
    for SCENE in text_stress geometry_stress compositing_stress; do
        if [ -z "${SCENE_FILTER}" ] || [ "${SCENE_FILTER}" = "${SCENE}" ]; then
            capture_one "${SCENE}" "${VIEWPORT}" "${ROTATION}" t1250 1.25
        fi
    done
done

echo "Android visual-parity captures: ${OUTPUT}"
