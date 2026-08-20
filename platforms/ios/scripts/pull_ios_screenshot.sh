#!/bin/sh
# WhatsCanvas iOS screenshot client.
#
# Pulls the most recent screenshot.png the demo wrote to its Documents
# container and opens it in Preview.app. Runs a `-w` loop when
# --watch is passed so the Mac keeps refreshing while the phone renders.
#
# Requires Xcode 15+ (devicectl file transfer support) and a paired iPhone.

set -eu

BUNDLE_ID="con.whatscanvas.demo"
REMOTE_NAME="screenshot.png"
REMOTE_METADATA="screenshot.json"
LOCAL_DIR="${TMPDIR:-/tmp}/whatscanvas-ios-screenshots"
DEVICE=""
WATCH=0
INTERVAL=2
OPEN_FILE=1

usage() {
    cat <<USAGE
Usage: $0 [--device UDID] [--watch] [--interval SECONDS] [--no-open]

Pull ${REMOTE_NAME} from ${BUNDLE_ID}'s Documents container into
${LOCAL_DIR}/. Without --device the first available iOS device is used.

Options:
  --device UDID     Target device UDID (default: first available)
  --watch           Loop, re-pulling every --interval seconds
  --interval N      Watch interval in seconds (default: ${INTERVAL})
  --no-open         Do not launch Preview.app on the pulled file
  -h, --help        Show this help
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --device) DEVICE="$2"; shift 2 ;;
        --watch) WATCH=1; shift ;;
        --interval) INTERVAL="$2"; shift 2 ;;
        --no-open) OPEN_FILE=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

if [ -z "${DEVICE}" ]; then
    DEVICE=$(xcrun devicectl list devices 2>/dev/null \
        | awk '/available \(paired\)/ {print $3; exit}')
    if [ -z "${DEVICE}" ]; then
        echo "No paired iOS device found. Pair a device or pass --device UDID." >&2
        exit 1
    fi
fi

mkdir -p "${LOCAL_DIR}"

pull_one() {
    local stamp
    stamp=$(date +"%Y%m%d-%H%M%S")
    local dest="${LOCAL_DIR}/${stamp}.png"
    xcrun devicectl device copy from \
        --device "${DEVICE}" \
        --domain-type appDataContainer \
        --domain-identifier "${BUNDLE_ID}" \
        --source "Documents/${REMOTE_NAME}" \
        --destination "${dest}" \
        >/dev/null 2>&1 || {
            echo "copy from failed (device=${DEVICE}, source=Documents/${REMOTE_NAME})" >&2
            return 1
        }
    # devicectl copies into ${dest}/${REMOTE_NAME} when the destination is
    # treated as a directory. Normalize to a single flat file.
    if [ -d "${dest}" ]; then
        mv "${dest}/${REMOTE_NAME}" "${dest}.tmp"
        rmdir "${dest}"
        mv "${dest}.tmp" "${dest}"
    fi
    local metadata_dest="${dest%.png}.json"
    xcrun devicectl device copy from \
        --device "${DEVICE}" \
        --domain-type appDataContainer \
        --domain-identifier "${BUNDLE_ID}" \
        --source "Documents/${REMOTE_METADATA}" \
        --destination "${metadata_dest}" \
        >/dev/null 2>&1 || true
    if [ -d "${metadata_dest}" ]; then
        mv "${metadata_dest}/${REMOTE_METADATA}" "${metadata_dest}.tmp"
        rmdir "${metadata_dest}"
        mv "${metadata_dest}.tmp" "${metadata_dest}"
    fi
    echo "${dest}"
}

if [ "${WATCH}" -eq 1 ]; then
    echo "Watching device=${DEVICE} interval=${INTERVAL}s (Ctrl-C to stop)"
    LAST=""
    while true; do
        if OUT=$(pull_one); then
            if [ "${OPEN_FILE}" -eq 1 ] && [ -z "${LAST}" ]; then
                open -a Preview "${OUT}"
            fi
            LAST="${OUT}"
            echo "$(date +%H:%M:%S) ${OUT}"
        fi
        sleep "${INTERVAL}"
    done
else
    OUT=$(pull_one)
    echo "${OUT}"
    if [ "${OPEN_FILE}" -eq 1 ]; then
        open -a Preview "${OUT}"
    fi
fi
