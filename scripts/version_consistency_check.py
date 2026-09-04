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
    getting_started = read("doc/public/getting-started/GETTING_STARTED_AS_LIBRARY.md")
    android_gradle = read("platforms/android/app/build.gradle")
    android_integration = read("doc/public/platforms/ANDROID_INTEGRATION.md")
    public_index = read("doc/public/index.md")
    changelog = read("CHANGELOG.md")
    package_workflow = read(".github/workflows/package-release.yml")

    versioned_public_docs = {
        "tutorial overview": read("doc/public/tutorials/README.md"),
        "tutorial overview (zh)": read("doc/public/tutorials/zh/README.md"),
        "tutorial intro": read("doc/public/tutorials/00-whatscanvas-intro.md"),
        "tutorial intro (zh)": read("doc/public/tutorials/zh/00-whatscanvas-intro.md"),
        "tutorial setup": read("doc/public/tutorials/01-environment-setup.md"),
        "tutorial setup (zh)": read("doc/public/tutorials/zh/01-environment-setup.md"),
        "tutorial backends": read("doc/public/tutorials/10-multi-backend.md"),
        "tutorial backends (zh)": read("doc/public/tutorials/zh/10-multi-backend.md"),
        "tutorial platforms": read("doc/public/tutorials/12-cross-platform.md"),
        "tutorial platforms (zh)": read("doc/public/tutorials/zh/12-cross-platform.md"),
    }

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
        "Android versionCode": require(r"versionCode\s+([0-9]+)", android_gradle, "Android versionCode"),
        "README badge": require(r"shields\.io/badge/version-([0-9]+\.[0-9]+\.[0-9]+)-", readme, "README version badge"),
        "README_zh badge": require(r"shields\.io/badge/version-([0-9]+\.[0-9]+\.[0-9]+)-", readme_zh, "README_zh version badge"),
        "README release example": require(r"whatscanvas-win64-release-([0-9]+\.[0-9]+\.[0-9]+)\.zip", readme, "README release example"),
        "README_zh release example": require(r"whatscanvas-win64-release-([0-9]+\.[0-9]+\.[0-9]+)\.zip", readme_zh, "README_zh release example"),
        "Android AAR example": require(r"whatscanvas-android-release-([0-9]+\.[0-9]+\.[0-9]+)\.aar", android_integration, "Android AAR example"),
        "CHANGELOG release": require(r"^## \[([0-9]+\.[0-9]+\.[0-9]+)\] - [0-9]{4}-[0-9]{2}-[0-9]{2}$", changelog, "CHANGELOG release"),
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
        "Android versionCode": str(int(major) * 10000 + int(minor) * 100 + int(patch)),
        "README badge": project_version,
        "README_zh badge": project_version,
        "README release example": project_version,
        "README_zh release example": project_version,
        "Android AAR example": project_version,
        "CHANGELOG release": project_version,
    }

    errors: list[str] = []
    for key, actual in checks.items():
        if actual != expected[key]:
            errors.append(f"{key}: expected {expected[key]}, got {actual}")

    if re.search(r'\$version\s*=\s*"[0-9]+\.[0-9]+\.[0-9]+"', package_workflow):
        errors.append("package-release workflow hardcodes $version; derive it from CMakeLists.txt instead")

    public_version_patterns = (
        r"find_package\s*\(\s*WhatsCanvas\s+([0-9]+\.[0-9]+\.[0-9]+)",
        r"whatscanvas-[a-z0-9-]+-release-([0-9]+\.[0-9]+\.[0-9]+)",
        r"WhatsCanvas\s+([0-9]+\.[0-9]+\.[0-9]+)\+",
    )
    for label, text in versioned_public_docs.items():
        for pattern in public_version_patterns:
            for match in re.finditer(pattern, text, re.IGNORECASE):
                if match.group(1) != project_version:
                    errors.append(
                        f"{label}: expected public version {project_version}, got {match.group(1)}"
                    )

    if re.search(r"github\.com/ClarkWain/WhatsCanvas/releases/tag/v[0-9]", public_index):
        errors.append("public docs index pins a release tag; link to the releases page instead")

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
