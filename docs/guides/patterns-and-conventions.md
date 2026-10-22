# Patterns and conventions

This catalog lists the design patterns Teserra uses, **where** it uses them, and the anti-patterns it rejects. Reviewers check new code against it.

> **Deviation rule.** Introducing a pattern that is not in this catalog, or using a listed anti-pattern, requires an accepted ADR or an RFC issue *before* the code is written. "It was faster" is not a reason; a documented trade-off is.

## Pattern catalog

| Pattern | Where it is used | Why | Rules |
|---|---|---|---|
| **RAII** | Everything that owns a resource: `Platform` (windowing init/terminate), `Window`, `Device`, `rhi::Unique<H>`, file handles, `DynamicLibrary`, profiling scopes, ECS iteration locks | Deterministic cleanup without exceptions or manual `release()` calls | One owner per resource; destructors never fail; move-only unless copying is meaningful |
| **Generational handle / index** | RHI resources, entities, assets, renderer materials and targets ([ADR-0020](../adr/0020-generational-handles.md)) | Safe references, stale detection, no backend IDs leaking, serializable | Handles are non-owning; lookups validate generation; distinct tag types per resource kind |
| **Object pool** | `HandlePool<T>` storage, ECS component pools, renderer per-frame objects, future particles | Stable allocation pattern, no per-object heap churn | Pools own their objects; growth only during warm-up in hot paths |
| **Abstract factory** | `platform::Platform` (creates windows, owns OS services), `rhi::createDevice()` (selects backend) | Swap implementations (GLFW/SDL, OpenGL/Vulkan/null) without changing callers | Selection by CMake option and runtime config, never by `#ifdef` in callers |
| **Factory method from descriptors** | `Device::create*(const *Desc&)`, `World::create()`, asset loaders | Immutable, validated creation; descriptors are plain data that can be logged, hashed and cached | Descriptors are aggregates with defaults (designated initializers); creation returns `Result` |
| **Command pattern** | `rhi::CommandList` (recorded GPU commands), `ecs::Commands` (deferred structural changes), future editor undo/redo | Decouple recording from execution; deterministic application; testable streams | Commands are POD records in reused buffers; execution point is explicit (submit, sync point) |
| **Strategy via interface** | `Window`, `FileSystem`, `Clock` (fake clock in tests), `AssetLoader<T>`, `RenderPass` | Replaceable behavior at well-defined seams | Interfaces are small (interface segregation); virtual calls only at coarse granularity, never per entity/vertex |
| **Interface segregation** | `OpenGlContext` separate from `Window`; `Device` vs `Swapchain`; read-only `const` views | Callers depend only on what they use; GL-specific capability isolated | Prefer several small interfaces over one large one |
| **Pimpl** | Only where a public header would otherwise expose a third-party type or heavy include (e.g. a GLFW-backed class that must be in a public header) | Keep third-party headers out of public API | Not used by default: interfaces + factories usually avoid the need. Each use is justified in a comment |
| **Observer / event queue** | Platform `EventQueue`, ECS `Events<T>` (double-buffered), future asset hot-reload notifications | Decoupled communication without callbacks re-entering engine internals | Queues are pulled, not pushed; no callbacks invoked from inside another subsystem's loop |
| **Dependency injection via explicit parameters** | `App` constructs services and passes references; systems receive `SystemContext&`; renderer receives `rhi::Device&` | Testability, no hidden coupling, multiple instances possible | No service locators, no singletons; constructor parameters or context objects only |
| **Data-oriented ECS** | `teserra_ecs`, render extraction into `FramePacket`, sprite batching | Cache-friendly batch processing, simple data ownership | Components are plain data; logic lives in systems; no virtual methods on components |
| **Double buffering** | ECS events, frame contexts (`kMaxFramesInFlight`), input current/previous state | Safe reading while writing the next frame | Buffer swap points are explicit and documented |
| **Facade** | Logging (`TESERRA_LOG_*` over spdlog), math aliases over glm, profiling macros over Tracy | Third-party replaceable, compile-time cost contained | Facade headers include no third-party headers unless the type must be public (glm) |
| **State cache** | Inside the OpenGL backend only (avoid redundant GL state changes) | Performance of an emulation layer | Never visible outside the backend; the RHI API has no mutable global state |

## Anti-patterns

| Anti-pattern | Why it is rejected | Use instead |
|---|---|---|
| **God objects** (`Engine` with 80 methods, `Renderer` that also loads files and handles input) | Untestable, every change touches everything | Small services with single responsibilities wired by `App` |
| **Global mutable state** | Hidden coupling, impossible to run two instances (tests, editor previews), thread-unsafe | Explicit ownership + references; world resources |
| **Ad-hoc singletons** (`Renderer::get()`, `static Logger instance`) | Same as globals, plus initialization-order bugs | Pass the object; the logging facade is the single documented exception (process-wide sinks, stateless call sites) |
| **Leaking GL (or any backend) types** | Makes the Vulkan backend a renderer rewrite | RHI types and handles only ([rhi.md](../architecture/rhi.md#what-must-not-leak-out-of-the-rhi)) |
| **Leaking platform types** (`HWND`, `GLFWwindow*`, GLFW key codes) | Ties upper layers to one OS/library | `NativeWindowHandle`, engine enums |
| **Deep inheritance trees** (`Entity → Actor → Pawn → Character → Player`) | Fragile base classes, poor locality, rigid design | Composition via components; interfaces at most one level deep |
| **Virtual calls per element** (per entity, per sprite, per vertex) | Kills batching and inlining | Batch processing over contiguous arrays |
| **Raw owning pointers / manual `delete`** | Leaks and double frees | `std::unique_ptr`, containers, pools, RAII owners |
| **Exceptions for control flow** in engine code | Hidden control flow ([ADR-0016](../adr/0016-error-handling-policy.md)) | `Result<T>`, assertions |
| **Stringly-typed lookups in hot paths** | Hashing and allocation every frame | Handles, `StringId` resolved once |
| **Callbacks re-entering a subsystem during its own iteration** | Iterator invalidation, ordering bugs | Event queues, deferred commands |
| **Speculative abstraction** ("we might need it for 3D") with no v1 user | Unexercised code rots and misleads | Design the seam, implement when used; every abstraction has a test or example |
| **Copy-paste backends** (duplicating validation in each backend) | Divergent behavior | Validation lives in RHI common code, backends assume valid input |

## Conventions that are not patterns but are enforced the same way

- **Descriptor structs** are aggregates with sensible defaults (every field, [coding standards](coding-standards.md#classes)), created with designated initializers, passed by `const&`, never stored by reference.
- **Ownership in signatures:** `std::unique_ptr<T>` = transfer ownership; `T&` = required, non-owning, outlives the call; `T*` = optional, non-owning; `std::span`/`std::string_view` = borrowed ranges for the duration of the call.
- **Thread affinity** is documented on every class that is not freely usable from any thread (`/// Main thread only.`).
- **Spikes** produce knowledge, not production code: spike code lives on a `spike/<topic>` branch and is thrown away or rewritten under the normal rules ([development process](../process/development-process.md#spikes)).
