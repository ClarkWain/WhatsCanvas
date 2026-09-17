# WhatsCanvas 1.2.0 dependency review

Review date: 2026-09-17. This records the local release preparation, not a
completed cross-platform release or a comprehensive vulnerability scan.

## Revisions and redistribution

The following checked-out revisions match the repository pins. There are no
changes to bundled dependency revisions, `.gitmodules`, or
`THIRD_PARTY_NOTICES.md` relative to `v1.1.0`.

| Component | Revision | Bundled license |
|---|---|---|
| FreeType | `0a0221a1347e2f1e07c395263540026e9a0aa7c7` | FreeType License |
| HarfBuzz | `56feae4035bdd48f62ba2b8d8c16232d4d89b3a4` | Old MIT |
| GLFW | `b00e6a8a88ad1b60c0a045e696301deb92c9a13e` | zlib/libpng |
| GLM | `6f14f4792a0cde5d0cf2c910506724d61cb95834` | MIT option |
| stb | `31c1ad37456438565541f4919958214b6e762fb4` | MIT option |

The locally built Windows install tree passed
`scripts/verify_desktop_release_artifact.py`: package version 1.2.0, headers,
CMake package metadata, libraries, project license and third-party notices are
present. The installed license directory also contains the full license texts
for the five dependencies above. Official per-platform assets still require
their package workflow and artifact-layout checks.

## Historical security follow-up

The [1.0 dependency audit](../1.0/dependency-audit.md) records an upstream
HarfBuzz pre-context concern and its call-site assessment. The current
`src/text/HarfBuzzTextShaper.cpp` still passes an owned normalized UTF-8 buffer,
offset zero, and the full buffer length to `hb_buffer_add_utf8`.

On this review date, GitHub's global-advisory API returned HTTP 404 for both
`GHSA-q4gc-p4hh-3765` and `GHSA-xvjr-f2r9-c7ww`, which were cited by that
historical audit. Their current affected ranges and remediation status could
not be independently confirmed. A 404 is not evidence that an advisory was
resolved or that the dependency has no vulnerabilities. Do not reuse the
historical audit as a fresh security clearance; retain this uncertainty for
maintainer review before tagging.
