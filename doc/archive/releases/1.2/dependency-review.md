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

The global-advisory API initially returned HTTP 404 for the two historical
IDs. Follow-up against the upstream repository advisory pages resolved this
uncertainty; a global API 404 alone is not a security result.

- [GHSA-q4gc-p4hh-3765](https://github.com/harfbuzz/harfbuzz/security/advisories/GHSA-q4gc-p4hh-3765)
  identifies versions before 14.0.0 as affected and 14.0.0 as patched.
  The pinned HarfBuzz 14.2.1 includes upstream bounds-check fix
  `c34dd6e24bd76591e423807616b2b8412d4a5df1`.
- [GHSA-xvjr-f2r9-c7ww](https://github.com/harfbuzz/harfbuzz/security/advisories/GHSA-xvjr-f2r9-c7ww)
  lists an affected range before 12.3.0. Independently of the version metadata,
  the pin includes upstream fix `1265ff8d990284f04d8768f35b0e20ae5f60daae`.

Both fixes were verified as ancestors of the exact pinned commit with
`git merge-base --is-ancestor`. No dependency update is required for these two
specific findings. This review does not claim that all possible dependency
vulnerabilities have been ruled out.
