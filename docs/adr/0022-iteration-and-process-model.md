# ADR-0022: Two-week iterations with one headline feature

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [development process](../process/development-process.md), [release process](../process/release-process.md)

## Context

Teserra is built by one developer with 5-10 hours per week, with occasional contributors and outside testers. The process must produce visible progress, collect feedback early, and keep quality high without consuming the little time available.

## Decision

- **Two-week iterations**, defined in the GitHub Project's `Iteration` field. Capacity per iteration: 10-20 h; **committed work is capped at 16 h** (≈20% buffer).
- **Exactly one headline feature per iteration**, tracked as an epic issue with sub-issues. Every iteration ends with something runnable (example app or demo), and a tagged pre-release when meaningful.
- Issues are sized **1-6 h**; anything larger is split before it becomes `Ready`.
- Unfinished work returns to the backlog. **Iterations are never extended.**
- A **feedback loop** runs on designated iterations: published build → feedback request issue with specific questions → triage into the backlog at the next planning.
- A short **review + retrospective** is written in the iteration's retro issue (template) at the end of every iteration.
- **Definition of Ready / Done**, the pattern catalog and design-note requirements apply regardless of time pressure ([development process](../process/development-process.md)).
- The backlog and roadmap live **only** in GitHub Issues and the GitHub Project, never as files in the repository.

## Alternatives considered

| Option | Why not |
|---|---|
| Kanban without iterations | Lower overhead, but loses the regular demo/feedback cadence that keeps a solo project motivated and visible. |
| One-week iterations | 5-10 h is too little to finish a headline feature plus ceremonies. |
| Scrum with full ceremonies | Ceremony overhead would eat 10-20% of a 15 h iteration. Planning + review/retro are kept to ~1 h total. |
| Backlog in Markdown files | Duplicates GitHub's tools, drifts, cannot be filtered or linked to PRs. |

## Consequences

**Positive**
- Predictable cadence, frequent demos, natural checkpoints for scope control.

**Negative / costs**
- Some headline features must be cut thinner than ideal to fit 16 h.

## Revisit if

- Velocity is consistently far above 16 h (raise the cap) or below 10 h for three iterations (shrink iteration scope or switch to three-week iterations).
