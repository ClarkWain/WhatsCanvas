# Security Policy

## Supported Versions

Until 1.0, security fixes target the latest `0.9.x` release. After 1.0, the
latest `1.x` release line is supported. Older pre-1.0 releases are unsupported
unless a release notice states otherwise.

## Reporting a Vulnerability

Use GitHub's private **Report a vulnerability** flow for this repository. If
that option is unavailable, open a public issue that requests a private contact
channel but contains no vulnerability details. Do not publish exploit details,
sensitive input, or an unpatched proof of concept.

Include, where applicable:

- the affected WhatsCanvas version, backend, platform, architecture, and build options;
- a minimal reproducer or triggering input;
- expected and observed behavior;
- sanitized crash logs or stack traces; and
- the expected impact and whether the issue is already public.

Maintainers will acknowledge the report, attempt to reproduce it, assess the
affected versions, prepare a fix and regression test, and coordinate disclosure
with the reporter. Ordinary correctness and performance bugs should use the
public issue tracker.
