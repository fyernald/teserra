# Glossary

Terms as they are used in Teserra's code and documentation.

## Engine and architecture

| Term | Meaning |
|---|---|
| **Layer** | One level of the dependency hierarchy (core L0 ... editor L7). A module may depend only on strictly lower layers. See [overview](architecture/overview.md#layers). |
| **Module** | One engine library = one CMake target (`teserra_<module>`), one directory under `engine/`. |
| **Facade** | A thin engine-owned API in front of a third-party library (logging over spdlog, math over glm). |
| **Generational handle** | `{index, generation}` reference to an object in a pool; stale handles are detected by comparing generations. [ADR-0020](adr/0020-generational-handles.md). |
| **`Result<T>`** | Return type of fallible functions: a value or an `Error`. [ADR-0016](adr/0016-error-handling-policy.md). |
| **Null backend** | RHI implementation without a GPU; records and validates commands for tests. |
| **Readiness (3D / Vulkan / Linux)** | A design constraint that keeps a future addition cheap; tracked as a Project field and in issue notes. |

## Rendering

| Term | Meaning |
|---|---|
| **RHI** | Render Hardware Interface: Teserra's backend-neutral GPU API. [rhi.md](architecture/rhi.md). |
| **Backend** | An RHI implementation (OpenGL, null; Vulkan later). |
| **DSA** | OpenGL Direct State Access: editing objects by name without binding them. Used only inside the OpenGL backend. |
| **Device** | RHI factory and owner of GPU resources; submits work. |
| **Swapchain** | The set of presentable images for a window. |
| **Descriptor (`*Desc`)** | Plain struct describing an object to create (`BufferDesc`, `GraphicsPipelineDesc`). Not to be confused with Vulkan descriptor sets. |
| **Graphics pipeline** | Immutable object containing shaders, vertex layout and all fixed-function state (blend, depth, raster) plus target formats. |
| **Bind group / bind group layout** | A set of resources (buffers, textures+samplers) bound together at one set index; the layout describes its shape. Equivalent to Vulkan descriptor sets/layouts. |
| **Pipeline layout** | Ordered list of bind group layouts a pipeline uses. |
| **Command list** | Recorded sequence of GPU commands, submitted to the device. Teserra records into a backend-agnostic stream. |
| **Render pass (RHI)** | A `beginRenderPass`/`endRenderPass` scope with attachments and load/store operations. |
| **Render pass (renderer)** | A renderer-level step (Scene2D, Debug, Composite) implementing `RenderPass`; may contain several RHI render passes. |
| **Render graph** | Future system that orders passes and manages transient resources automatically from declared inputs/outputs. |
| **Frame in flight** | A frame the CPU has submitted but the GPU may still be processing; Teserra allows 2. |
| **Fence** | CPU-GPU synchronization primitive signaled when submitted work completes. |
| **Ring buffer (upload)** | GPU-visible buffer partitioned per frame in flight for streaming per-frame data. |
| **Sort key** | 64-bit integer encoding draw order (layer, depth, material) for sorting draw items. |
| **Batch** | Consecutive draw items sharing material and texture, drawn with one call. |
| **Frame packet** | Plain render data extracted from the scene for one frame. |
| **Render target** | Offscreen color (+ depth) textures a camera renders into. |
| **Composite pass** | Final pass that draws the scene target onto the swapchain (with scaling). |
| **NDC / clip space** | Normalized device coordinates after projection; Teserra: depth `[0, 1]`, +Y up. [ADR-0024](adr/0024-coordinate-conventions.md). |
| **sRGB** | Non-linear color encoding; color textures and the swapchain use sRGB formats, shading math is linear. |
| **SPIR-V** | Binary intermediate shader language consumed by Vulkan (and OpenGL via `ARB_gl_spirv`). |
| **glslang** | Reference GLSL compiler, produces SPIR-V. |
| **SPIRV-Cross** | Library that reflects SPIR-V and translates it to GLSL/HLSL/MSL. |
| **`.tshader`** | Teserra shader blob: SPIR-V + backend translations + reflection, produced by `teserra_shaderc`. |
| **Golden image** | Reference image for a snapshot test. |
| **Pixels per unit** | How many texture pixels map to one world unit for sprites. |

## ECS and scenes

| Term | Meaning |
|---|---|
| **ECS** | Entity Component System: entities are IDs, components are plain data, systems are functions over component sets. |
| **World** | Container of entities, component pools, resources and events. |
| **Entity** | Generational ID of a thing in a world. |
| **Component** | Plain data attached to an entity. A component without data is a **tag**. |
| **Sparse set** | Storage with a sparse index array and dense packed arrays; Teserra's component pool. [ADR-0019](adr/0019-ecs-storage-sparse-sets.md). |
| **Archetype** | Alternative ECS storage grouping entities by component set (not used in v1). |
| **View** | Iteration over entities having certain components (and lacking excluded ones). |
| **System** | Callable that runs each frame/phase over views, with declared data access. |
| **Schedule / phase** | Ordered collection of systems; phases are `Startup`, `PreUpdate`, `FixedUpdate`, `Update`, `PostUpdate`, `Extract`, `Last`. |
| **Commands** | Deferred structural changes (create/destroy/add/remove) applied at sync points. |
| **Resource (ECS)** | Typed per-world singleton such as `Time`. Not to be confused with GPU resources. |
| **Event** | Typed message in a double-buffered per-world queue. |
| **Scene** | A world plus its schedule and scene resources; managed by the `SceneManager`. |
| **Extraction** | Copying render-relevant data from the ECS into the frame packet. |
| **Tileset / tilemap / chunk** | Tile graphics + metadata / grid of tile indices in layers / fixed-size block of a tilemap rendered with one cached buffer. |

## Assets

| Term | Meaning |
|---|---|
| **Asset** | Loadable content (texture, shader, material, tileset, map). |
| **`AssetId`** | Persistent identity of an asset (hash of its virtual path in v1). |
| **`Asset<T>`** | Runtime, reference-counted handle to a loaded asset. |
| **Loader** | Object that turns bytes into an asset of one type. |
| **Mount point** | Virtual path prefix (`assets://`, `engine://`) mapped to a directory or archive. |
| **Fallback asset** | Placeholder used when loading fails (e.g. magenta checkerboard). |
| **Cooker** | Future offline tool converting source assets to runtime formats. |

## Process

| Term | Meaning |
|---|---|
| **ADR** | Architecture Decision Record in `docs/adr/`. |
| **RFC** | Request For Comments issue for a proposal that needs discussion before an ADR. |
| **Iteration** | Two-week planning period (Project field `Iteration`). |
| **Headline feature** | The one substantial, demoable capability of an iteration, tracked as an epic. |
| **Epic** | Tracking issue for a headline feature or future track, with sub-issues. |
| **Spike** | Time-boxed research task; its code is thrown away. |
| **DoR / DoD** | Definition of Ready / Done. [development process](process/development-process.md). |
| **Design note** | Short design sketch required before coding changes to public interfaces, the RHI or the ECS. |
| **Velocity** | Sum of estimates completed in an iteration. |
| **Feedback request** | Issue asking testers specific questions about a published build. |
| **Pre-release** | Tagged iteration build (`v0.1.0-alpha.N`). |
| **vcpkg baseline** | Pinned commit of the vcpkg registry that fixes dependency versions. |
| **Preset** | Named CMake configure/build/test configuration in `CMakePresets.json`. |
