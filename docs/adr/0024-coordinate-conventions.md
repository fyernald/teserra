# ADR-0024: Coordinate system and clip-space conventions

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [ADR-0004](0004-opengl-45-dsa-first-backend.md), [ADR-0008](0008-glm-math-behind-aliases.md), [RHI](../architecture/rhi.md#conventions), [renderer](../architecture/renderer.md#3d-readiness-checklist)

## Context

OpenGL and Vulkan disagree on clip-space depth range (GL `[-1, 1]` by default, Vulkan `[0, 1]`), on the direction of clip-space Y, and on the origin of framebuffers and textures (GL lower-left, Vulkan/D3D/Metal upper-left). If these differences leak into the renderer, every shader and render-to-texture path breaks when the Vulkan backend arrives. Fixing conventions now costs almost nothing; fixing them later touches everything.

## Decision

**World and math**
- World space is **right-handed, +Y up**, +X right, +Z toward the viewer; cameras look down −Z (matches glTF and glm defaults).
- Matrices are **column-major with column vectors** (`v' = M * v`), as in glm and GLSL.
- Angles in **radians**; positive 2D rotation is counter-clockwise around +Z.
- 2D content lives in the same 3D world (z = 0 plane by default); 2D cameras are orthographic cameras looking down −Z. Sprite size in world units derives from texture size and a per-asset `pixelsPerUnit` setting.

**Clip space and render targets (RHI contract, all backends)**
- Clip-space depth range is **`[0, 1]`** (`GLM_FORCE_DEPTH_ZERO_TO_ONE`). Reverse-Z with a floating-point depth buffer is the planned default for 3D (v2) and is possible because of this choice.
- Engine projection matrices produce **+Y up** in normalized device coordinates.
- Render targets, textures, viewports, scissor rectangles, texture uploads and readbacks use a **top-left origin**: row 0 is the top row of the image, like image files, Vulkan, D3D12 and Metal. UV `(0, 0)` is the top-left texel.
- Front faces are **counter-clockwise** as seen by the viewer.

**Backend obligations**
- OpenGL backend (planned mechanism; the flip is implemented in iteration 4 and verified by readback tests in iteration 7): `glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE)` for all rendering so rendered images are stored top-row-first; the swapchain is an internal color texture that is presented with a **vertically flipped blit** to the default framebuffer; front-face winding is configured so the RHI contract holds.
- Vulkan backend (future): negative-height viewport (core in Vulkan 1.1) to map +Y-up NDC to Vulkan's framebuffer orientation.

**Verification**
- An **orientation test** renders a quad with a distinct top-left marker both to the swapchain and to an offscreen texture; readback must show the marker in row 0 / column 0 for the texture and in the top-left of the window. A **winding test** draws a CCW and a CW triangle with back-face culling enabled.

## Alternatives considered

| Option | Why not |
|---|---|
| GL defaults everywhere (`[-1, 1]` depth, lower-left origin) | Vulkan backend would need projection and UV fix-ups spread across the renderer; reverse-Z impossible with good precision. |
| Left-handed world (D3D/Unity style) | Conflicts with glTF and glm defaults, making the 3D asset pipeline do conversions. |
| Y-down 2D world (screen space, Godot style) | Convenient for UI and some 2D games, but a separate 2D convention would fork the transform system and break 3D readiness. Users who want screen-space coordinates get helpers on the 2D camera. |

## Consequences

**Positive**
- Shaders, UVs and render-to-texture paths behave identically on GL and Vulkan.
- The only GL-specific flip lives in one place (swapchain present).

**Negative / costs**
- One full-screen blit per frame on GL (negligible for v1 resolutions).
- Developers familiar with GL tutorials must flip mental models for UVs; documented in the renderer guide.

## Revisit if

- The orientation/winding tests reveal that the planned `glClipControl` mechanism behaves differently across drivers; the decision (RHI contract) stays, only the GL mechanism changes.
