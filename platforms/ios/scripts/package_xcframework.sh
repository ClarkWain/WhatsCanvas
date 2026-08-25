#!/bin/sh
set -eu

REPOSITORY_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
OUTPUT_ROOT="${WHATSCANVAS_IOS_PACKAGE_DIR:-${REPOSITORY_ROOT}/out/mobile/ios}"
BUILD_ROOT="${OUTPUT_ROOT}/build"

VERSION="$(sed -n 's/.*project[[:space:]]*(.*WhatsCanvas[[:space:]]*VERSION[[:space:]]*\([0-9][0-9.]*\).*/\1/p' "${REPOSITORY_ROOT}/CMakeLists.txt" | head -n 1)"
if [ -z "${VERSION}" ]; then
  echo "Unable to read WhatsCanvas version from CMakeLists.txt" >&2
  exit 1
fi

ARTIFACT_NAME="whatscanvas-ios-release-${VERSION}"
PACKAGE_ROOT="${OUTPUT_ROOT}/${ARTIFACT_NAME}"
XCFRAMEWORK_PATH="${PACKAGE_ROOT}/WhatsCanvas.xcframework"
ARCHIVE_PATH="${OUTPUT_ROOT}/${ARTIFACT_NAME}.zip"

rm -rf "${BUILD_ROOT}" "${PACKAGE_ROOT}" "${ARCHIVE_PATH}"
mkdir -p "${BUILD_ROOT}" "${PACKAGE_ROOT}"

build_slice() {
  slice_name="$1"
  sysroot="$2"
  architectures="$3"
  build_directory="${BUILD_ROOT}/${slice_name}"

  cmake -S "${REPOSITORY_ROOT}" -B "${build_directory}" \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="${sysroot}" \
    -DCMAKE_OSX_ARCHITECTURES="${architectures}" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_BUILD_TYPE=Release \
    -DWHATSCANVAS_BUILD_OPENGL=OFF \
    -DWHATSCANVAS_BUILD_OPENGLES=OFF \
    -DWHATSCANVAS_BUILD_METAL=ON \
    -DWHATSCANVAS_BUILD_SOFTWARE=OFF \
    -DWHATSCANVAS_BUILD_DEMO=OFF \
    -DWHATSCANVAS_BUILD_DESKTOP_PLATFORM=OFF \
    -DWHATSCANVAS_BUILD_BENCHMARKS=OFF \
    -DWHATSCANVAS_ENABLE_METAL=ON \
    -DWHATSCANVAS_ENABLE_VULKAN=OFF \
    -DWHATSCANVAS_ENABLE_OPENTYPE_SHAPING=OFF \
    -DWHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=OFF \
    -DWHATSCANVAS_ENABLE_SCRIPT_TESTS=OFF \
    -DWHATSCANVAS_INSTALL=OFF \
    -DBUILD_TESTING=OFF >&2

  cmake --build "${build_directory}" --target WhatsCanvasMetal --parallel >&2

  library_path="$(find "${build_directory}" -name 'libWhatsCanvasMetal.a' -type f -print -quit)"
  if [ -z "${library_path}" ]; then
    echo "Missing libWhatsCanvasMetal.a for ${slice_name}" >&2
    exit 1
  fi
  printf '%s\n' "${library_path}"
}

DEVICE_LIBRARY="$(build_slice device iphoneos arm64)"
SIMULATOR_LIBRARY="$(build_slice simulator iphonesimulator 'arm64;x86_64')"

lipo "${DEVICE_LIBRARY}" -verify_arch arm64
lipo "${SIMULATOR_LIBRARY}" -verify_arch arm64 x86_64

link_consumer() {
  sdk="$1"
  target="$2"
  library_path="$3"
  output_path="$4"
  sdk_path="$(xcrun --sdk "${sdk}" --show-sdk-path)"

  xcrun --sdk "${sdk}" clang++ \
    -std=c++17 \
    -target "${target}" \
    -isysroot "${sdk_path}" \
    -I"${REPOSITORY_ROOT}/include" \
    "${REPOSITORY_ROOT}/platforms/ios/sdk-consumer-smoke/main.mm" \
    "${library_path}" \
    -framework Metal \
    -framework Foundation \
    -framework QuartzCore \
    -framework CoreGraphics \
    -framework CoreText \
    -framework UIKit \
    -o "${output_path}"
}

link_consumer iphoneos arm64-apple-ios15.0 "${DEVICE_LIBRARY}" \
  "${BUILD_ROOT}/device-consumer-smoke"
link_consumer iphonesimulator arm64-apple-ios15.0-simulator \
  "${SIMULATOR_LIBRARY}" "${BUILD_ROOT}/simulator-consumer-smoke"

xcodebuild -create-xcframework \
  -library "${DEVICE_LIBRARY}" -headers "${REPOSITORY_ROOT}/include" \
  -library "${SIMULATOR_LIBRARY}" -headers "${REPOSITORY_ROOT}/include" \
  -output "${XCFRAMEWORK_PATH}"

cp "${REPOSITORY_ROOT}/LICENSE" "${PACKAGE_ROOT}/LICENSE"
cp "${REPOSITORY_ROOT}/THIRD_PARTY_NOTICES.md" "${PACKAGE_ROOT}/THIRD_PARTY_NOTICES.md"
cp "${REPOSITORY_ROOT}/platforms/ios/SDK_README.md" "${PACKAGE_ROOT}/README.md"
mkdir -p "${PACKAGE_ROOT}/licenses/freetype" \
  "${PACKAGE_ROOT}/licenses/glfw" \
  "${PACKAGE_ROOT}/licenses/glm" \
  "${PACKAGE_ROOT}/licenses/harfbuzz" \
  "${PACKAGE_ROOT}/licenses/stb"
cp "${REPOSITORY_ROOT}/third_party/freetype/LICENSE.TXT" \
  "${REPOSITORY_ROOT}/third_party/freetype/docs/FTL.TXT" \
  "${PACKAGE_ROOT}/licenses/freetype/"
cp "${REPOSITORY_ROOT}/third_party/glfw/LICENSE.md" \
  "${PACKAGE_ROOT}/licenses/glfw/"
cp "${REPOSITORY_ROOT}/third_party/glm/copying.txt" \
  "${PACKAGE_ROOT}/licenses/glm/"
cp "${REPOSITORY_ROOT}/third_party/harfbuzz/COPYING" \
  "${PACKAGE_ROOT}/licenses/harfbuzz/"
cp "${REPOSITORY_ROOT}/third_party/stb/LICENSE" \
  "${PACKAGE_ROOT}/licenses/stb/"

ditto -c -k --sequesterRsrc --keepParent "${PACKAGE_ROOT}" "${ARCHIVE_PATH}"
printf 'Created %s\n' "${ARCHIVE_PATH}"
