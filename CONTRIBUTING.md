# Contributing to Teserra

Thanks for your interest! Teserra is a learning-driven but quality-first open-source engine. This guide explains how work is organized and what a good contribution looks like. Please also read the [Code of Conduct](CODE_OF_CONDUCT.md).

## Ways to contribute

- **Try iteration builds and answer feedback requests.** This is the most valuable help early on (see [below](#giving-feedback-on-iteration-builds)).
- **Report bugs** with the *Bug report* template.
- **Improve documentation**: typos, unclear sections, missing explanations.
- **Write code** for `Ready` issues in the [GitHub Project](https://github.com/users/fyernald/projects/3).
- **Propose changes** to design via an *RFC* issue.

## Finding work

1. Open the [GitHub Project](https://github.com/users/fyernald/projects/3) and use the **Backlog** or **By area** view.
2. Pick an issue with Status **Ready** that is not assigned. Issues labeled **`good first issue`** are scoped for newcomers; **`help wanted`** marks issues the maintainer would especially like help with.
3. Comment "I'd like to work on this" and wait for the maintainer to assign you (usually within a few days). This avoids duplicate work.
4. Do not start on `Backlog` items or epics without discussing first: they may not be designed yet.

### Good first issue policy

An issue gets `good first issue` only if it:

- is estimated at **≤ 3 h** and touches one module,
- needs **no design note** (no public interface, RHI or ECS changes),
- has acceptance criteria and pointers to the files involved,
- has a maintainer willing to answer questions on it.

## Setting up

Follow [build and CI](docs/guides/build-and-ci.md). Short version (Windows):

```bash
git clone https://github.com/fyernald/teserra.git
cd teserra
cmake --workflow --preset windows-msvc-debug
```

Run checks before pushing:

```bash
cmake --build --preset windows-msvc-debug --target format check
ctest --preset windows-msvc-debug
```

## Workflow

- **Trunk-based development.** `main` is always buildable, tested and demoable. Work happens on short-lived branches and lands through pull requests.
- **Branch names:** `<type>/<issue-number>-<short-slug>`, e.g. `feat/42-sprite-batcher`, `fix/57-swapchain-resize`, `docs/12-rhi-binding-model`. Spikes use `spike/<topic>` and are never merged.
- **Commits:** [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) with the area as scope:

  ```text
  feat(rhi): add sampler creation to the OpenGL backend
  fix(ecs): keep sparse index valid after swap-and-pop removal
  docs(adr): add ADR-0025 audio library
  ```

  Types: `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `ci`, `chore`. Reference the issue in the body (`Refs #42`).
- **Pull requests:** fill in the template, link the issue with `Closes #N`, keep PRs focused (one issue per PR when possible). Draft PRs are welcome early.
- **Merging:** squash merge after CI is green and the checklist is satisfied. The PR title becomes the commit message, so it must follow the commit style.

## Quality rules

Fast iterations never relax these. Details in the [development process](docs/process/development-process.md):

- [Definition of Done](docs/process/development-process.md#definition-of-done): CI green, tests, warnings clean, format/tidy clean, docs and CHANGELOG updated, no layer violations.
- [Coding standards](docs/guides/coding-standards.md) and [naming conventions](docs/adr/0021-naming-conventions.md).
- [Pattern catalog](docs/guides/patterns-and-conventions.md): use the listed patterns; deviations need an ADR or RFC first.
- **Design notes:** changes to public interfaces, the RHI or the ECS need a short [design note](docs/process/development-process.md#design-notes) agreed in the issue before coding.
- **ADRs** for new dependencies and architectural changes ([when an ADR is required](docs/adr/README.md#when-an-adr-is-required)).
- **Layering:** a module may only depend on strictly lower layers; no OpenGL outside the OpenGL backend; no GLFW/Win32 outside the platform layer ([overview](docs/architecture/overview.md#how-the-layering-is-enforced)).
- **Tests** as required by the [testing strategy](docs/guides/testing-strategy.md#what-every-pr-needs).

## PR checklist

This mirrors the [pull request template](.github/PULL_REQUEST_TEMPLATE.md):

- [ ] Linked issue; acceptance criteria met
- [ ] Builds without warnings; CI green
- [ ] `format` and `check` targets pass; clang-tidy clean on changed files
- [ ] Tests added/updated; GPU tests run locally for rendering changes
- [ ] No layer violations or backend/platform leaks
- [ ] Docs/ADR updated if design changed; public API documented with `///`
- [ ] CHANGELOG entry for user-visible changes
- [ ] Demo/example still works

## Giving feedback on iteration builds

Some iterations publish a pre-release build for testers. Each comes with a **Feedback request** issue (label `feedback`) that links the download and asks specific questions.

1. Download the zip from the linked GitHub pre-release and follow its `README.txt`.
2. Answer the questions in a comment on the feedback request. Include your OS, GPU and driver version.
3. For crashes or clear bugs, open a separate *Bug report* and link it in your comment.

Feedback is triaged at the next iteration planning; the feedback request is closed with a summary of what changed because of it.

## Licensing of contributions

Teserra is MIT licensed. By submitting a contribution, you agree that it is licensed under the same [MIT License](LICENSE) (inbound = outbound). There is no CLA. Only submit work you have the right to contribute.
