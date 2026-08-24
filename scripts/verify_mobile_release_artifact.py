#!/usr/bin/env python3
"""Verify the public layout of WhatsCanvas Android and iOS release assets."""

from __future__ import annotations

import argparse
import io
import json
import plistlib
import zipfile
from pathlib import Path


ANDROID_ABIS = ("armeabi-v7a", "arm64-v8a", "x86_64")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def verify_android(path: Path) -> None:
    require(path.is_file(), f"Android AAR not found: {path}")
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        required = {
            "META-INF/LICENSE",
            "META-INF/THIRD_PARTY_NOTICES.md",
            "META-INF/licenses/freetype/FTL.TXT",
            "META-INF/licenses/harfbuzz/COPYING",
            "prefab/prefab.json",
            "prefab/modules/whatscanvas/module.json",
            "prefab/modules/whatscanvas/include/wsc/wsc.h",
        }
        for abi in ANDROID_ABIS:
            required.add(
                f"prefab/modules/whatscanvas/libs/android.{abi}/libwhatscanvas.so"
            )
            required.add(f"jni/{abi}/libwhatscanvas.so")
            required.add(f"jni/{abi}/libc++_shared.so")
        missing = sorted(required - names)
        require(not missing, "Android AAR is missing: " + ", ".join(missing))
        prefab = json.loads(archive.read("prefab/prefab.json"))
        require(prefab.get("name") == "whatscanvas", "Prefab package name must be 'whatscanvas'")
    print(f"Android release artifact verified: {path}")


def verify_ios(path: Path) -> None:
    require(path.is_file(), f"iOS archive not found: {path}")
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        plist_names = [name for name in names if name.endswith("WhatsCanvas.xcframework/Info.plist")]
        require(len(plist_names) == 1, "Expected one WhatsCanvas.xcframework/Info.plist")
        prefix = plist_names[0][: -len("Info.plist")]
        package_prefix = prefix[: -len("WhatsCanvas.xcframework/")]
        require(f"{package_prefix}LICENSE" in names, "iOS archive is missing LICENSE")
        require(
            f"{package_prefix}THIRD_PARTY_NOTICES.md" in names,
            "iOS archive is missing THIRD_PARTY_NOTICES.md",
        )
        require(
            f"{package_prefix}licenses/harfbuzz/COPYING" in names,
            "iOS archive is missing bundled dependency licenses",
        )
        info = plistlib.load(io.BytesIO(archive.read(plist_names[0])))
        libraries = info.get("AvailableLibraries", [])
        require(len(libraries) == 2, "XCFramework must contain device and simulator slices")

        saw_device = False
        saw_simulator = False
        for library in libraries:
            identifier = library.get("LibraryIdentifier", "")
            library_path = library.get("LibraryPath", "")
            headers_path = library.get("HeadersPath", "")
            require(library.get("SupportedPlatform") == "ios", f"Unexpected platform: {identifier}")
            require(library_path.endswith(".a"), f"Slice is not a static library: {identifier}")
            require(f"{prefix}{identifier}/{library_path}" in names, f"Missing library: {identifier}")
            require(
                f"{prefix}{identifier}/{headers_path}/wsc/wsc.h" in names,
                f"Missing public headers: {identifier}",
            )
            architectures = set(library.get("SupportedArchitectures", []))
            if library.get("SupportedPlatformVariant") == "simulator":
                saw_simulator = architectures == {"arm64", "x86_64"}
            else:
                saw_device = architectures == {"arm64"}
        require(saw_device, "XCFramework is missing the arm64 device slice")
        require(saw_simulator, "XCFramework is missing the arm64/x86_64 simulator slice")
    print(f"iOS release artifact verified: {path}")


def main() -> int:
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--android-aar", type=Path)
    group.add_argument("--ios-zip", type=Path)
    args = parser.parse_args()
    if args.android_aar:
        verify_android(args.android_aar)
    else:
        verify_ios(args.ios_zip)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
