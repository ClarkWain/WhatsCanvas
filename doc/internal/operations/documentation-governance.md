# Documentation Governance

This policy defines where documentation belongs, what is authoritative, and
when it must be reviewed. It applies to repository documentation and the
published MkDocs site.

## Sources of truth

| Content | Authoritative location | Lifecycle |
| --- | --- | --- |
| User behavior, setup, API, platform support | `doc/public/` | Current; reviewed with every affected release |
| Public API declarations | `include/wsc/` and generated API reference | Generated; never hand-edit the reference |
| Durable engineering decisions | `doc/internal/architecture/` | Keep while the decision still shapes the implementation |
| Active product or performance work | `doc/internal/backlog/` | Unresolved outcomes only |
| Dated reviews and validation evidence | `doc/internal/design-reviews/`, `doc/internal/validation/` | Snapshot; include a date and scope |
| Completed release and implementation history | `doc/archive/` | Immutable except for link repair or factual annotation |

## Lifecycle

1. Start uncertain analysis as a dated design review.
2. Promote durable accepted rules into an architecture document.
3. Put user-visible behavior in public documentation before claiming support.
4. Track unfinished work as an issue and, when useful, summarize the active
   outcome in the internal backlog.
5. On completion, remove the backlog checkbox and preserve only evidence that
   helps reproduce a result or understand a decision.
6. Archive superseded release records and long implementation narratives; do
   not leave them in current guides.

## Review triggers

- Public API or behavior change: update the relevant guide and regenerate the
  API reference.
- Platform/backend support change: update the public support matrix and dated
  validation evidence.
- Release: review public versions, package examples, active backlog, release
  checklist, and navigation.
- Architecture reversal: update or replace the decision record and explain the
  replacement; do not silently leave conflicting accepted documents.

## Naming and links

- Published paths are stable URLs. Do not rename a public document without a
  redirect or an explicit compatibility decision.
- New internal files use lowercase kebab-case names. Dated snapshots include
  `YYYY-MM` or `YYYY-MM-DD` when their conclusions can age.
- Link to one authoritative page instead of duplicating the same contract in
  several guides.
- Avoid hard-coded release versions outside files covered by the version
  consistency check.

## Required checks

- `uv run --with-requirements requirements-docs.txt mkdocs build --strict`
- `scripts/api_reference_check.*`
- `scripts/version_consistency_check.*`
- repository-relative Markdown link scan when files move
- `examples/tutorials/sync_docs.ps1 -Check` when tutorial examples change

## Confidentiality

`doc/internal/` means unpublished, not confidential. In a public repository,
credentials, embargoed security findings, customer information, and private
commercial plans must live in an access-controlled system outside this tree.
