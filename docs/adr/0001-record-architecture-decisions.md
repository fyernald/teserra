# ADR-0001: Record architecture decisions

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [ADR index](README.md), [development process](../process/development-process.md)

## Context

Teserra is a solo-led open-source engine that should stay understandable to contributors who arrive months or years later. Engine architecture is a long chain of trade-offs (RHI shape, ECS storage, handle layout); without a record, decisions get silently re-litigated or silently violated.

## Decision

We will record every significant decision as a Markdown ADR in `docs/adr/`, using [`0000-template.md`](0000-template.md). ADRs are numbered sequentially, immutable once accepted, and superseded rather than edited. Discussion-heavy proposals start as an RFC issue and end as an ADR.

## Alternatives considered

| Option | Why not |
|---|---|
| Decisions only in issues/PR threads | Hard to find, no single current state, lost when threads get long. |
| A single `DECISIONS.md` file | Merge conflicts, no per-decision history, encourages editing old decisions in place. |
| Wiki | Not versioned with the code, not reviewed via PRs. |

## Consequences

**Positive**
- New contributors can learn *why* the engine looks the way it does.
- The pattern catalog and DoD can point at ADRs as the authority for deviations.

**Negative / costs**
- Writing an ADR takes 20-60 minutes. This is budgeted inside the issue that needs it.

## Revisit if

ADRs are routinely skipped or become stale. That is a process problem to discuss in a retrospective, not a reason to drop ADRs.
