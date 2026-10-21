## Summary

<!-- What does this PR change and why? One or two sentences. -->

Closes #

## Type

<!-- feat / fix / refactor / perf / test / docs / build / ci / chore (matches the PR title prefix) -->

## Design

<!-- For public interface, RHI or ECS changes: link the design note or ADR. Otherwise "n/a". -->

## Checklist (Definition of Done)

- [ ] Acceptance criteria of the linked issue are met
- [ ] Builds without warnings; CI is green
- [ ] `format` and `check` targets pass; clang-tidy clean on changed files
- [ ] Tests added/updated ([what every PR needs](../docs/guides/testing-strategy.md#what-every-pr-needs))
- [ ] Rendering changes: GPU tests / examples run locally (state GPU + driver below)
- [ ] No layer violations; no OpenGL outside the GL backend; no GLFW/Win32 outside the platform layer
- [ ] Follows the [pattern catalog](../docs/guides/patterns-and-conventions.md) (deviations have an ADR/RFC)
- [ ] Docs updated (architecture page / ADR / public `///` comments) where design or API changed
- [ ] `CHANGELOG.md` updated for user-visible changes (**BREAKING:** noted with migration hint)
- [ ] Examples/demo still run
- [ ] 3D / Vulkan / Linux readiness considered (see the issue's future-proofing note)

## How was this tested?

<!-- Commands run, tests added, hardware for GPU tests (e.g. "RTX 3060, driver 581.xx, Windows 11"). -->

## Screenshots / captures

<!-- For visual changes: before/after images, or golden image diffs. -->
