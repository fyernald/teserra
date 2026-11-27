# Release process

How Teserra versions, tags, packages and announces builds. Related: [development process](development-process.md#feedback-loop), [ADR-0018 license](../adr/0018-license-mit.md).

## Versioning

- **Semantic Versioning.** Before 1.0, minor versions (`0.x`) may contain breaking changes; every breaking change is listed in the changelog with migration notes. Patch versions (`0.x.y`) are bug fixes only.
- Version numbers are defined once in the root `CMakeLists.txt` (`project(Teserra VERSION 0.1.0)`) and exposed as `teserra::kVersion` and `TESERRA_VERSION_STRING`.
- Planned versions: `v0.1` (first public 2D release), `v0.2` (debug tools, Linux, audio), `v0.3` (editor start, physics), `v1.0` (stable 2D API), `v2.0` (3D renderer, Vulkan), `v3.0+` (scripting). The GitHub Project's *Target version* field is authoritative for what goes where.

## Tags

**Tag only when a build is actually published.** A tag exists to back a GitHub Release that someone will download. Finishing an iteration, merging a feature or archiving a branch is not a reason to tag. Every tag is a SemVer version, so the Releases page reads as a clean version history.

| Tag | When | Example |
|---|---|---|
| `v<next>-alpha.<n>` | A pre-release build handed to testers with a feedback request | `v0.1.0-alpha.1`, then `v0.1.0-alpha.2` |
| `v<next>-rc.<n>` | Release candidate during release prep | `v0.1.0-rc.1` |
| `v<version>` | Release | `v0.1.0` |

- Pre-release numbers count up from 1 for each version (`alpha.1`, `alpha.2`, …). They are not iteration numbers; the release notes name the iteration.
- No other tags. Spike branches are not kept with `archive/*` tags: their findings go in the spike issue or an ADR before the branch is deleted.
- Tags are annotated (`git tag -a`) and created on `main` only, from a commit with green CI. The version in the root `CMakeLists.txt` equals the tag's version without its pre-release part (`v0.1.0-alpha.1` needs `VERSION 0.1.0`).
- Pushing a tag runs `.github/workflows/release.yml`. It rejects a tag that breaks any rule above and otherwise drafts the GitHub Release with generated notes, marked *pre-release* when the tag has an `-alpha`/`-rc` part. Nothing is published until the draft is edited and published by hand.
- There are no long-lived release branches before 1.0. A patch release is cut from `main`; if `main` has moved on with unreleasable changes, a short-lived `release/0.1` branch is created from the tag and the fix is cherry-picked.

## Pre-release checklist (only when a build is published)

1. CI green on `main`; `CMakeLists.txt` has the version being pre-released; the iteration's demo runs from a clean build (`cmake --workflow --preset windows-msvc-release`).
2. `CHANGELOG.md`: the `Unreleased` section lists the iteration's user-visible changes.
3. **For feedback iterations, open the *Feedback request* issue first** (template). Its build link is the release's URL, `https://github.com/fyernald/teserra/releases/tag/v0.1.0-alpha.N` (N = the next pre-release number for that version), which the tag name fixes before the release exists; it works from step 6 on. The zip's `README.txt` links to this issue, so it has to exist before packaging. Iterations without a feedback round skip this step, and their zip links to the issue chooser.
4. Tag `v0.1.0-alpha.N` on the commit of step 1 and push the tag.
5. Package. Pushing the tag runs `release.yml`: it drafts the GitHub pre-release, builds Release on Windows and Linux, and attaches `teserra-<version>-windows-x64-examples.zip` and `teserra-<version>-linux-x64-examples.tar.gz`, whose `README.txt` links to the open `feedback` issue that has the release's URL in its body (step 3), or to the issue chooser. A feedback round about one example can add a zip of only that example: `python tools/scripts/package_example.py --build-dir build/windows-msvc-release --example <name> --version 0.1.0-alpha.N --feedback-url <feedback request of step 3>` (from a Visual Studio developer prompt, which locates the Visual C++ runtime), attached to the draft by hand.
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
| `teserra-<version>-windows-x64-examples.zip` | One folder per example with a window (executable, DLLs, assets, the Visual C++ runtime), and `README.txt`, `LICENSE`, `THIRD_PARTY_NOTICES.md` | `release.yml`, every version tag |
| `teserra-<version>-linux-x64-examples.tar.gz` (since v0.2) | One folder per example with a window (executable with mode 755, assets), and `README.txt`, `LICENSE`, `THIRD_PARTY_NOTICES.md`. Built on Ubuntu 24.04 with Clang (`linux-clang-release`); the executables link only the C and C++ runtimes, so no libraries are bundled | `release.yml`, every version tag; unpacked and run on Ubuntu 24.04 and 26.04 by the same workflow |
| `teserra-<version>-windows-x64-sdk.zip` (later) | The CMake install tree of #143 (headers, static libraries for Debug and Release, `TeserraConfig.cmake`), and the shader tool once it is installed | `release.yml` |

Until the SDK archive exists, games build Teserra from source with FetchContent or `add_subdirectory()`, or install it from a build of their own ([installing Teserra](../guides/build-and-ci.md#installing-teserra)).

Executables are not code-signed in v0.x; Windows SmartScreen may warn. The README of each zip explains this.

`tools/scripts/package_example.py --all-examples` makes the examples zip from a Release build (`--platform linux-x64`: the Linux archive). It refuses a debug build, a missing example, a DLL that `THIRD_PARTY_NOTICES.md` does not list, and on Linux a shared library next to an example or an executable that links more than the system's C and C++ runtimes (checked with `ldd`). `tools/scripts/smoke_linux_archive.sh` unpacks a Linux archive and runs every example for 60 frames; the release workflow runs it under Xvfb on Ubuntu 24.04 and 26.04. A pull request that changes the packaging (the workflow, the scripts, `tools/packaging/` or the notices) runs the package jobs as a dry run and uploads both archives, versioned `<version>-pr.<number>`, as workflow artifacts.

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
