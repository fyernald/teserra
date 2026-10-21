# Security policy

## Supported versions

Teserra is pre-1.0. Only the **latest release** and the current `main` branch receive security fixes.

| Version | Supported |
|---|---|
| latest `v0.x` release | Yes |
| older releases | No |
| `main` | Yes |

## Reporting a vulnerability

**Do not open a public issue for security problems.**

Report privately through GitHub's private vulnerability reporting:
**[Report a vulnerability](https://github.com/fyernald/tessera/security/advisories/new)**.

Please include the affected version or commit, a description of the issue, steps or a file to reproduce, and the impact you expect.

What to expect (best effort, this is a volunteer project):

- Acknowledgement within **7 days**.
- An assessment and a plan (fix, mitigation, or explanation) within **30 days**.
- Credit in the release notes if you wish.
- Coordinated disclosure: we publish an advisory after a fix is released.

## Scope and threat model

Teserra is a game engine library. Relevant security concerns:

- **Asset parsing.** v1 treats asset files (images, maps, shaders) as **trusted developer content** and uses decoders (e.g. stb_image) that are not hardened against hostile input. A game that loads untrusted files (user mods, downloads) must validate them or use hardened decoders. Reports of crashes on malformed assets are still welcome as bugs.
- **Build and supply chain.** Dependencies are pinned through a vcpkg baseline. Report compromised or vulnerable dependency versions.
- **Engine code.** Memory safety bugs (buffer overflows, use-after-free) in engine code are in scope.

Out of scope: vulnerabilities in GPU drivers or the operating system, and issues that require a modified engine build.
