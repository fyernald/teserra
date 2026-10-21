# Risks and open questions

Living document: assumptions made while setting up the project, known risks with mitigations, concerns about fixed decisions, and questions the maintainer must answer. Update it in retros; resolve items into ADRs or issues.

## Open questions for the maintainer

| # | Question | Default assumed until answered | Needed by |
|---|---|---|---|
| Q1 | ~~**Name and repository.** Is "Teserra" intentional or should it be "Tessera"? Rename the repo to `teserra`?~~ **Resolved 2026-10-21 ([#2](https://github.com/fyernald/teserra/issues/2)):** the name stays **Teserra**. The name check found no software, game-engine or package-registry use of this spelling; the only TESERRA trademarks cover landscaping. "Tessera" is crowded. The repository is `fyernald/teserra` (old `fyernald/tessera` URLs redirect); namespace `teserra`, macro prefix `TESERRA_`, targets `teserra_*` / `Teserra::*`. | - | - |
| Q2 | **Legal name in copyright.** LICENSE says "fyernald and the Teserra contributors". Use a legal name? | GitHub handle | Before v0.1 |
| Q3 | **Contact address** for Code of Conduct reports (must be private, not an issue). | Reports go through GitHub private vulnerability reporting (enabled 2026-10-21) with a "Conduct:" title prefix until a dedicated address is published | Before first external contributors |
| Q4 | ~~**GitHub Discussions** for Q&A and long-form feedback?~~ **Resolved 2026-10-21:** Discussions enabled for questions and ideas; feedback on builds is still collected in feedback-request issues so it can be triaged. | - | - |
| Q5 | **Exam/holiday calendar.** Which weeks should be light iterations or breaks? | Continuous 2-week iterations from 2026-10-26; iteration 5 overlaps the year-end holidays | Next planning |
| Q6 | **Reference machine** for the performance budget (CPU/GPU). | Author's development machine, documented when the first benchmark lands | Iteration 8 |
| Q7 | **MIT vs zlib** final confirmation (relicensing is only cheap before external contributions). | MIT ([ADR-0018](adr/0018-license-mit.md)) | Before the first external PR is merged |
| Q8 | **Minimum Windows version and GPU** to state as supported (Windows 10 22H2+? GL 4.5 GPUs from ~2014+). | Windows 10/11 x64, any GPU with an OpenGL 4.5 core driver | Iteration 5 feedback round |

## Assumptions made

- The author's upcoming Monday is **2026-10-26**; iteration 1 starts then.
- Capacity is ~15 h per iteration on average; committed work is capped at 16 h. v0.1 is planned for **16 iterations (iteration 16 ends 2027-06-06)** rather than the 11 originally sketched, because the original headline features did not fit 16 h each without cutting quality work (tests, CI, design notes). Real velocity after three iterations may stretch this further; the Project roadmap is re-planned, not the quality rules.
- Testers are reachable through the author's network and communities (e.g. r/gameenginedevs, Discord servers); no paid testing.
- All third-party dependencies are available as vcpkg ports for `x64-windows` (and `x64-linux` for the future job).
- v0.1 targets developers comfortable with C++ and CMake; no binary SDK installer, only a CMake package/zip.
- Assets are trusted developer content in v1 (no hardened parsing of untrusted files).
- The editor and scripting do not ship in v0.1; ImGui debug tools are v0.2.

## Concerns about fixed decisions

None of the fixed decisions is considered a serious mistake. The following deserve monitoring:

| Decision | Concern | Mitigation / trigger |
|---|---|---|
| OpenGL 4.5 first, Vulkan-style RHI | The GL backend must emulate explicit concepts (replay, binding remap, fences, swapchain blit). This is more work than a GL-shaped wrapper and can delay early demos. | RHI split across two iterations (clear, then triangle); spike on the command-list model in iteration 2. Trigger: if the GL backend exceeds its estimates by >50% twice, cut v1 RHI scope (e.g. defer readback/dynamic offsets), not the abstraction. |
| glm "wrapped or aliased so it can be replaced" | Aliases do not make glm truly replaceable (operators, member functions). | Accepted: "replaceable with moderate effort"; glm use confined to core by CI ([ADR-0008](adr/0008-glm-math-behind-aliases.md)). |
| Custom ECS from scratch | Correctness bugs and time cost in a central system. | Randomized model tests, ASan job, benchmark-gated optimization, and a stop rule in [ADR-0007](adr/0007-custom-ecs.md). |
| Windows-first with GLFW | GLFW lacks some features (rich gamepads, IME). | Interfaces allow SDL3 as a contained replacement ([ADR-0006](adr/0006-glfw-behind-engine-interfaces.md)). |
| Testing framework: the brief allowed doctest or Catch2 | Catch2 v3 chosen for maintenance and built-in benchmarks; slightly slower builds than doctest. | Revisit trigger in [ADR-0012](adr/0012-testing-framework-catch2.md). |

## Risks

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| **Scope creep** ("while I'm here, let me add 3D lighting") | High | High | One headline feature per iteration; non-goals in every issue; 3D readiness is a *checklist*, not 3D implementation; new ideas go to the backlog. |
| **Abstraction leaking** (GL types or assumptions in renderer/RHI API) | Medium | High | CI leak checks, null backend tests, "what must not leak" list in [rhi.md](architecture/rhi.md#what-must-not-leak-out-of-the-rhi), review checklist item. |
| **Over-abstraction** (Vulkan/3D-ready code that v1 never exercises) | Medium | Medium | Every abstraction must have a v1 user or test; "speculative abstraction" is an anti-pattern in the catalog. |
| **OpenGL ↔ Vulkan semantic gaps** | High (known) | Medium | Gaps are enumerated and owned by the GL backend: coordinate conventions (clip control + flip blit, orientation tests), `(set, binding)` remap shared with the shader tool, implicit vs explicit synchronization (implicit RHI in v1), render pass load/store emulation, push constants (future, UBO emulation), pipeline creation cost (lazy creation + cache), descriptor allocation (Vulkan pools later), resource lifetime (deferred destruction by frame slot), command recording thread affinity (stream design). |
| **Clip-control / orientation behaves differently across drivers** | Low-Medium | Medium | Orientation and winding readback tests (iteration 7) run on the maintainer's GPUs; the iteration 8 public build shows an on-screen top-left marker so every tester confirms orientation visually. |
| **Shader toolchain complexity** (glslang + SPIRV-Cross builds, binding remap) | Medium | Medium | Spike before implementation; fallback documented in [ADR-0017](adr/0017-shader-pipeline.md); binary caching in CI. |
| **No GPU in CI** | High (known) | Medium | Null backend for most tests; spike on software rasterizer; local GPU test requirement in PR checklist until solved. |
| **Solo burnout / life events** (exams, holidays) | Medium | High | 16 h cap with buffer, light iterations and breaks are explicitly allowed, no iteration extensions, demos every iteration for motivation. |
| **Estimates too optimistic** (first-time implementation of RHI/ECS) | High | Medium | Split rule (>50% overrun → split), velocity-based planning after 3 iterations, buffer. |
| **vcpkg baseline breakage / slow first build** | Medium | Low | Pinned baseline, separate bump PRs, binary cache. |
| **Driver bugs on specific GPUs** | Medium | Medium | Early hardware feedback build (iteration 5), KHR_debug output in logs, testers report GPU/driver. |
| **Name collision** (existing products named Tessera/Teserra) | Low (checked in [#2](https://github.com/fyernald/teserra/issues/2)) | Medium | Name check done in iteration 1. Final trademark check in release prep ([#123](https://github.com/fyernald/teserra/issues/123)), including the EUIPO and WIPO Global Brand Database searches that #2 could not run (classes 9, 28, 41). |
| **Bus factor of 1** | High | Medium | Docs-first project: ADRs, architecture docs, contributor guide; CI enforces rules so they don't depend on memory. |
| **Dependency vulnerabilities** (stb_image with untrusted input) | Low (v1 trusted assets) | Medium | Documented in SECURITY.md; hardened decoders before loading untrusted content. |

## Decisions deferred on purpose

| Topic | Deferred to | Why |
|---|---|---|
| Audio library (miniaudio is the leading candidate) | v0.2 epic, ADR then | Not needed for v0.1 headline features |
| Physics (Box2D v3 is the leading candidate) | v0.3 epic | Demo game uses simple AABB collision |
| JSON library for Tiled import (nlohmann/json vs simdjson vs yyjson) | Tilemap iteration, ADR in the import issue | Only needed there |
| Text rendering approach (stb_truetype bitmap fonts vs MSDF) | Backlog spike | Debug text in v0.2 |
| Job system design | v1.0 track | Single-threaded v1 |
| Render graph | v2.0 | Pass declarations in v1 keep it possible |
