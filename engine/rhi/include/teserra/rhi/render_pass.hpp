#pragma once

// Render passes: the attachments a sequence of draws renders into, with what happens to their contents
// at the start (LoadOp) and the end (StoreOp) of the pass. There are no framebuffer objects; backends cache
// what they need. RenderPassDesc is copied into the command stream, so it and everything it contains is
// trivially copyable (docs/architecture/rhi.md#render-passes).

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/rhi/limits.hpp>

namespace teserra::rhi {

/// What an attachment holds at the start of a render pass.
enum class LoadOp : u8 {
    /// The previous contents.
    Load,
    /// The clear value.
    Clear,
    /// Undefined: every texel is written by the pass anyway. Cheapest on tiled GPUs.
    DontCare,
};

/// What happens to an attachment's contents at the end of a render pass.
enum class StoreOp : u8 {
    /// Kept for later passes, sampling or presenting.
    Store,
    /// May be discarded, e.g. a depth buffer only needed during the pass.
    DontCare,
};

/// One color target of a render pass.
struct ColorAttachment {
    /// A texture created with TextureUsage::RenderTarget, or a swapchain image. Null for an unused slot.
    TextureHandle texture{};
    u32 mipLevel = 0;
    /// Layer of an array texture, face of a cube.
    u32 arrayLayer = 0;
    LoadOp loadOp = LoadOp::Load;
    StoreOp storeOp = StoreOp::Store;
    /// Used with LoadOp::Clear, in the texture's color space before encoding (linear for sRGB formats).
    Vec4 clearColor{0.0F, 0.0F, 0.0F, 1.0F};
};

/// The depth-stencil target of a render pass.
struct DepthStencilAttachment {
    /// A texture created with TextureUsage::DepthStencil. Null for a pass without depth.
    TextureHandle texture{};
    u32 mipLevel = 0;
    u32 arrayLayer = 0;
    LoadOp depthLoadOp = LoadOp::Clear;
    StoreOp depthStoreOp = StoreOp::DontCare;
    /// Used with LoadOp::Clear; 1 is the far plane.
    f32 clearDepth = 1.0F;
    LoadOp stencilLoadOp = LoadOp::DontCare;
    StoreOp stencilStoreOp = StoreOp::DontCare;
    u32 clearStencil = 0;
};

/// The attachments of a render pass.
///
///     cmd.beginRenderPass({
///         .colorAttachments = {{
///             .texture = backbuffer,
///             .loadOp = LoadOp::Clear,
///             .clearColor = {0.1F, 0.1F, 0.12F, 1.0F},
///         }},
///     });
struct RenderPassDesc {
    /// The attachments in use are the leading ones with a non-null texture; after the first null slot,
    /// every slot must be null.
    // A C array: with std::array, designated initializers of the elements need a third pair of braces.
    ColorAttachment colorAttachments[kMaxColorAttachments]{}; // NOLINT(modernize-avoid-c-arrays)
    DepthStencilAttachment depthStencil{};
};

} // namespace teserra::rhi
