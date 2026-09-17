# WhatsCanvas 1.2.0 validation record

Release candidate rendering/library sources: `ecadb7d9747ad29356fdf646fe3d04f2235783f9`.
The release integration carries that candidate plus CodeQL reporting and
clang-tidy toolchain configuration, a direct standard-library header include,
clarified GL test documentation, and this evidence record. Release and PR
workflow results must also be checked on the final integration/tag commits.

## Automated checks

- [Cross-Platform Validation](https://github.com/ClarkWain/WhatsCanvas/actions/runs/35216989496): all 16 jobs passed, including Windows/Linux/macOS unit jobs, OpenGL and GLES parity, ASan/UBSan, TSan, fuzzing, Web, Vulkan, Android and iOS.
- [Package WhatsCanvas](https://github.com/ClarkWain/WhatsCanvas/actions/runs/35216989602): all five platform jobs passed, including package-consumer checks.
- [CodeQL](https://github.com/ClarkWain/WhatsCanvas/actions/runs/35216989493): analysis completed successfully; this is not a claim of zero findings.
- All five downloaded candidate assets passed archive-integrity and repository desktop/mobile layout gates, including versions and license files.
- Local Windows release preflight: 73 unit tests and four engineering gates passed before the CI portability follow-ups. The three directly affected tests passed again after those follow-ups.
- The overlapping-layer regression adds 29 analytic checks per GL-family backend. Before the clipping fix, 17 of these checks failed; after the fix all passed.
- Local ASan probe for the owned Fontconfig configuration completed three font enumerations without a leak report. The full Linux ASan/UBSan job subsequently passed.

Hosted Windows/macOS runners without OpenGL 3.3 explicitly skip the two new
GL-context-dependent tests. Their Software/native-text checks run where
applicable. Linux unit and sanitizer jobs require the GL context, so those
pixel checks cannot silently skip there. Mesa software rendering is not a
physical-device GPU result.

## Mobile hardware

The maintainer confirmed Android physical-device validation for this release
in the release session on 2026-09-17. Device model and detailed measurements
were not supplied; no additional device coverage is claimed here. Historical
iOS hardware evidence remains documented in the stable-v1 sign-off; the current
iOS packaging and consumer validation passed in CI.

## CodeQL report scope

The manual C++ analysis continues to include bundled dependency code, but
findings located under `third_party/` are excluded before SARIF upload. Build
and analysis failures and first-party findings remain visible. The pinned
filter was tested on the real candidate report: 984 third-party findings were
removed from 1101 results; all 117 first-party results were preserved.

See [dependency review](dependency-review.md) for the independently verified
HarfBuzz fix ancestry and redistribution checks.
