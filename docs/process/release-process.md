# Release process

How Teserra versions, tags, packages and announces builds. Related: [development process](development-process.md#feedback-loop), [ADR-0018 license](../adr/0018-license-mit.md).

## Versioning

- **Semantic Versioning.** Before 1.0, minor versions (`0.x`) may contain breaking changes; every breaking change is listed in the changelog with migration notes. Patch versions (`0.x.y`) are bug fixes only.
- Version numbers are defined once in the root `CMakeLists.txt` (`project(Teserra VERSION 0.1.0)`) and exposed as `teserra::kVersion` and `TESERRA_VERSION_STRING`.
- Planned versions: `v0.1` (first public 2D release), `v0.2` (debug tools, Linux, audio), `v0.3` (editor start, physics), `v1.0` (stable 2D API), `v2.0` (3D renderer, Vulkan), `v3.0+` (scripting). The GitHub Project's *Target version* field is authoritative for what goes where.

## Tags

| Tag | When | Example |
|---|---|---|
| `v<next>-alpha.<iteration>` | End of an iteration whose demo is worth trying | `v0.1.0-alpha.5` (iteration 5) |
| `v<next>-rc.<n>` | Release candidate during release prep | `v0.1.0-rc.1` |
| `v<version>` | Release | `v0.1.0` |

- Tags are annotated (`git tag -a`) and created on `main` only, from a commit with green CI.
- Pre-release tags are marked as *pre-release* on GitHub Releases.
- There are no long-lived release branches before 1.0. A patch release is cut from `main`; if `main` has moved on with unreleasable changes, a short-lived `release/0.1` branch is created from the tag and the fix is cherry-picked.

## Pre-release checklist (end of iteration)

1. CI green on `main`; the iteration's demo runs from a clean build (`cmake --workflow --preset windows-msvc-release`).
2. `CHANGELOG.md`: the `Unreleased` section lists the iteration's user-visible changes.
3. **For feedback iterations, open the *Feedback request* issue first** (template). Its build link is the release's URL, `https://github.com/fyernald/teserra/releases/tag/v0.1.0-alpha.N`, which the tag name fixes before the release exists; it works from step 6 on. The zip's `README.txt` links to this issue, so it has to exist before packaging. Iterations without a feedback round skip this step, and their zip links to the issue chooser.
4. Tag `v0.1.0-alpha.N` on the commit of step 1 and push the tag.
5. Package. The `release.yml` workflow builds the Windows x64 Release artifacts and drafts a GitHub pre-release; it takes the feedback request's URL as an input (#122). Until it exists, build the Release preset, make the zip with `python tools/scripts/package_example.py --build-dir build/windows-msvc-release --example <name> --version 0.1.0-alpha.N --feedback-url <feedback request of step 3>` (from a Visual Studio developer prompt, which locates the Visual C++ runtime), and draft the pre-release by hand with it.
6. Edit the draft: short summary, link to the iteration epic, known issues, and how to give feedback (the feedback request). Publish. The feedback request's build link now works; share it ([feedback loop](development-process.md#feedback-loop)).

## Release checklist (`v0.x.0`)

1. All issues with the target version are closed or explicitly moved to the next version.
2. Release-prep items done: API review, getting-started guide, name/trademark check, license and third-party notices audit.
3. Tag `v0.x.0-rc.1`; ask testers for a smoke test (feedback request). Fix blockers, repeat with `rc.2` if needed.
4. Move `Unreleased` in `CHANGELOG.md` to `## [0.x.0] - YYYY-MM-DD`; update README status and supported-platform table.
5. Tag `v0.x.0`, publish the GitHub Release with the changelog section as notes.
6. Announce (README badge/links, communities), then open the next version's first planning.

## Release artifacts

| Artifact | Content | From |
|---|---|---|
| Source archive | Automatic GitHub archive | Tag |
| `teserra-<version>-windows-x64-examples.zip` | Example and demo executables, assets, `README.txt`, `LICENSE`, `THIRD_PARTY_NOTICES.md` | `release.yml` |
| `teserra-<version>-windows-x64-sdk.zip` (from v0.1.0) | CMake install tree: headers, static libraries (Debug + Release), `TeserraConfig.cmake`, shader tool | `release.yml` |

Executables are not code-signed in v0.x; Windows SmartScreen may warn. The README of each zip explains this.

## Changelog policy

- `CHANGELOG.md` follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) with sections `Added`, `Changed`, `Deprecated`, `Removed`, `Fixed`, `Security`.
- Every PR with a **user-visible** change adds a line under `## [Unreleased]` (PR checklist item). Internal refactors, CI and docs-only changes do not, unless they change how users build or use the engine.
- Breaking changes are prefixed with **BREAKING:** and include a one-line migration hint.
- Each entry ends with the PR number: `- Sprite batcher with layer sorting (#123)`.
- GitHub's generated release notes (configured by `.github/release.yml`, grouped by `type:*` labels) are a starting point for the release description, not a replacement for the changelog.

## Third-party notices

`THIRD_PARTY_NOTICES.md` lists every dependency that ends up in a binary (name, version, license, copyright, license text or link). It is updated in the same PR that adds or bumps a dependency and is shipped in every artifact. Games built with Teserra must ship Teserra's MIT notice and these notices; the getting-started guide shows how.

## Support policy

Before 1.0 only the latest release (and the current `main`) is supported. Security fixes follow [SECURITY.md](../../SECURITY.md).
