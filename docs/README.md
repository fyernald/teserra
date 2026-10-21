# Teserra documentation

Start with the [architecture overview](architecture/overview.md). The backlog and roadmap live in the [GitHub Project](https://github.com/users/fyernald/projects/3), not in this folder.

## Architecture

| Document | What it covers |
|---|---|
| [overview.md](architecture/overview.md) | Layers, modules, dependency rules and their enforcement, runtime structure, frame flow |
| [public-api.md](architecture/public-api.md) | How game developers use the engine; "dream code" for a tiny 2D game |
| [rhi.md](architecture/rhi.md) | Render Hardware Interface, binding model, command lists, what must not leak, OpenGL mapping |
| [renderer.md](architecture/renderer.md) | Frame structure, passes, cameras, sprite batcher, materials, render targets, 3D readiness checklist |
| [platform-abstraction.md](architecture/platform-abstraction.md) | Platform interfaces, selection mechanism, new platform port checklist |
| [ecs.md](architecture/ecs.md) | Entities, sparse-set storage, views, commands, scheduling, roadmap |
| [memory-and-performance.md](architecture/memory-and-performance.md) | Allocation rules, profiling, benchmarking policy, v1 performance budget |
| [assets.md](architecture/assets.md) | Asset IDs, handles, loaders, mount points, future cooker/hot reload |
| [editor-and-scripting-future.md](architecture/editor-and-scripting-future.md) | How v1 keeps the editor and scripting possible |

## Decisions

[Architecture Decision Records](adr/README.md): one file per decision, with context, alternatives and consequences.

## Guides

| Guide | For |
|---|---|
| [coding-standards.md](guides/coding-standards.md) | How code is written |
| [patterns-and-conventions.md](guides/patterns-and-conventions.md) | Which design patterns are used where; anti-patterns |
| [build-and-ci.md](guides/build-and-ci.md) | CMake, presets, vcpkg, CI jobs, layer/leak checks |
| [testing-strategy.md](guides/testing-strategy.md) | Unit, null-backend, GPU, snapshot tests, benchmarks |
| [repository-layout.md](guides/repository-layout.md) | Where everything lives |

## Process

| Document | For |
|---|---|
| [development-process.md](process/development-process.md) | Iterations, GitHub Project usage, DoR/DoD, design notes, spikes, feedback loop, retros |
| [release-process.md](process/release-process.md) | Versioning, tags, artifacts, changelog policy |

## Reference

- [glossary.md](glossary.md)
- [risks-and-open-questions.md](risks-and-open-questions.md)

Every document marks what is **v1** and what is **future**. If a document and the code disagree, the code is wrong or the document is stale: open an issue.
