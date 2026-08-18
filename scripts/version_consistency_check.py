#!/usr/bin/env python3
"""Check that public/package version declarations stay in sync."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(pattern: str, text: str, label: str) -> str:
    match = re.search(pattern, text, re.MULTILINE)
    if not match:
        raise RuntimeError(f"missing {label}")
    return match.group(1)


def main() -> int:
    cmake = read("CMakeLists.txt")
    version_h = read("include/wsc/Version.h")
    readme = read("README.md")
    readme_zh = read("README_zh.md")
    getting_started = read("doc/GETTING_STARTED_AS_LIBRARY.md")
    android_gradle = read("platforms/android/app/build.gradle")
    package_workflow = read(".github/workflows/package-release.yml")

    project_version = require(r"project\s*\(\s*WhatsCanvas\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", cmake, "CMake project version")
    major, minor, patch = project_version.split(".")

    checks = {
        "WSC_VERSION_MAJOR": require(r"#define\s+WSC_VERSION_MAJOR\s+([0-9]+)", version_h, "WSC_VERSION_MAJOR"),
        "WSC_VERSION_MINOR": require(r"#define\s+WSC_VERSION_MINOR\s+([0-9]+)", version_h, "WSC_VERSION_MINOR"),
        "WSC_VERSION_PATCH": require(r"#define\s+WSC_VERSION_PATCH\s+([0-9]+)", version_h, "WSC_VERSION_PATCH"),
        "WSC_VERSION_STRING": require(r'#define\s+WSC_VERSION_STRING\s+"([^"]+)"', version_h, "WSC_VERSION_STRING"),
        "README find_package": require(r"find_package\s*\(\s*WhatsCanvas\s+([0-9]+\.[0-9]+\.[0-9]+)\s+CONFIG\s+REQUIRED\s*\)", readme, "README find_package version"),
        "README_zh find_package": require(r"find_package\s*\(\s*WhatsCanvas\s+([0-9]+\.[0-9]+\.[0-9]+)\s+CONFIG\s+REQUIRED\s*\)", readme_zh, "README_zh find_package version"),
        "getting-started find_package": require(r"find_package\s*\(\s*WhatsCanvas\s+([0-9]+\.[0-9]+\.[0-9]+)\s+CONFIG\s+REQUIRED\s*\)", getting_started, "getting-started find_package version"),
        "Android versionName": require(r"versionName\s+['\"]([0-9]+\.[0-9]+\.[0-9]+)['\"]", android_gradle, "Android versionName"),
    }

    expected = {
        "WSC_VERSION_MAJOR": major,
        "WSC_VERSION_MINOR": minor,
        "WSC_VERSION_PATCH": patch,
        "WSC_VERSION_STRING": project_version,
        "README find_package": project_version,
        "README_zh find_package": project_version,
        "getting-started find_package": project_version,
        "Android versionName": project_version,
    }

    errors: list[str] = []
    for key, actual in checks.items():
        if actual != expected[key]:
            errors.append(f"{key}: expected {expected[key]}, got {actual}")

    if re.search(r'\$version\s*=\s*"[0-9]+\.[0-9]+\.[0-9]+"', package_workflow):
        errors.append("package-release workflow hardcodes $version; derive it from CMakeLists.txt instead")

    if errors:
        print("VERSION_CONSISTENCY_RESULT=FAIL")
        for error in errors:
            print(f"VERSION_CONSISTENCY_ERROR={error}")
        return 1

    print(f"VERSION_CONSISTENCY_VERSION={project_version}")
    print("VERSION_CONSISTENCY_RESULT=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
