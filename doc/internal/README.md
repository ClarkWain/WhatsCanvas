# Maintainer Documentation

This directory contains project-management and engineering evidence for
maintainers. It is intentionally excluded from the published documentation
site and is not part of the public product contract.

- `backlog/` contains optional expansion and improvement candidates. The stable
  v1 scope has no open capability or performance blocker; a candidate becomes
  active only after it receives an issue, owner, milestone, and acceptance
  threshold.
- `architecture/` contains the current maintainer decision records and design
  boundaries.
- `design-reviews/` contains dated readiness assessments, not current API
  documentation.
- `operations/` contains repeatable maintainer procedures.
- `research/` contains unpublished educational prototypes and background
  material that is not validated as current product documentation.
- `validation/` contains dated execution records and working matrices; public
  validation rules live in `../public/validation/`.

The consolidated representative Android/iOS hardware result is
[`mobile-hardware-signoff-2026-09.md`](validation/mobile-hardware-signoff-2026-09.md).

Do not place credentials, private customer information, unpublished security
findings, or other confidential material here. Files in this directory remain
visible when the Git repository is public.

See [`documentation-governance.md`](operations/documentation-governance.md) for
the source-of-truth map, lifecycle rules, and required checks.
