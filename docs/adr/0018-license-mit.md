# ADR-0018: License: MIT

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [LICENSE](../../LICENSE), [release process](../process/release-process.md#third-party-notices)

## Context

Teserra targets indie developers who ship commercial games, possibly on consoles later, and wants outside contributors. The license must be permissive, widely understood, and compatible with all planned dependencies (glm MIT, spdlog MIT, GLFW zlib, glad MIT/Apache-2.0 generated code, stb public domain/MIT, Catch2 BSL-1.0, tl-expected CC0, Dear ImGui MIT, glslang BSD-style, SPIRV-Cross Apache-2.0, Tracy BSD-3).

## Options compared

| | MIT | Apache-2.0 | zlib |
|---|---|---|---|
| Familiarity for contributors and companies | Highest | High | Medium (well known in game dev: SDL, GLFW, raylib) |
| Obligation for shipped games | Include copyright + license text (e.g. in credits/legal screen or a text file) | Include license + NOTICE file; state changes | **None for binaries**; source redistributions keep the notice |
| Explicit patent grant | No | **Yes** | No |
| GPLv2 compatibility | Yes | No | Yes |
| Length / readability | Very short | Long | Very short |

## Decision

**MIT License.** Copyright line: `Copyright (c) 2026 fyernald and the Teserra contributors`.

Rationale: it is the most recognized permissive license (lowest friction for contributors, companies and portfolio reviewers), compatible with every dependency, and its only obligation for game developers (ship the license text) is light and identical to what our dependencies already require. Apache-2.0's patent grant matters less for a hobby-scale graphics engine than its NOTICE overhead and GPLv2 incompatibility. zlib's "no attribution in binaries" is attractive, but in practice games must ship third-party notices for dependencies anyway.

Contributions are accepted under the same license (inbound = outbound, stated in CONTRIBUTING). No CLA.

## Consequences

**Positive**
- Simple for everyone; no legal review needed for most adopters.

**Negative / costs**
- No explicit patent grant.
- Shipped games must include the MIT notice (we document how, and ship a `THIRD_PARTY_NOTICES.md` with releases).

## Revisit if

- Users report attribution as a real adoption barrier: relicensing to zlib is cheap **only while there are few external contributors**, so this must be decided before v0.1 (tracked in the v0.1 release-prep iteration).
- A corporate contributor requires an explicit patent grant (consider MIT OR Apache-2.0 dual licensing).
