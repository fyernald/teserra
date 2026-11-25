#pragma once

// Snapshot tests (docs/guides/testing-strategy.md#snapshot-tests): render at a fixed resolution into an
// offscreen texture, read it back, and compare it with tests/golden/<name>.png. A failure writes the actual
// image and a diff image to tests/golden/_actual/ and _diff/ (git-ignored; CI uploads them). Running the
// test program with --update-goldens writes the actual images as the new goldens instead.

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/renderer/render_pass.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/handles.hpp>

#include "image_compare.hpp"

#include <functional>
#include <memory>
#include <string_view>

namespace teserra::snapshot {

/// The size of every snapshot.
inline constexpr UVec2 kSnapshotSize{160, 90};

/// Set by the test program's --update-goldens option.
void setUpdateGoldens(bool update) noexcept;
[[nodiscard]] bool updateGoldens() noexcept;

/// Compares `actual` with the golden of `name` within `tolerance` (a Catch2 check), or replaces the golden
/// with --update-goldens.
void checkSnapshot(std::string_view name, const Image& actual, const Tolerance& tolerance = {});

/// A hidden-window App on the GPU backend of the tests (TESERRA_GPU_BACKEND), with nearest sprite
/// filtering, and an offscreen Rgba8Unorm output of kSnapshotSize that render() reads back. Skips the test,
/// or fails it when GPU tests are required, without a driver.
class SnapshotRenderer {
public:
    SnapshotRenderer();
    ~SnapshotRenderer();

    SnapshotRenderer(const SnapshotRenderer&) = delete;
    SnapshotRenderer& operator=(const SnapshotRenderer&) = delete;
    SnapshotRenderer(SnapshotRenderer&&) = delete;
    SnapshotRenderer& operator=(SnapshotRenderer&&) = delete;

    [[nodiscard]] engine::App& app() noexcept { return *m_app; }
    [[nodiscard]] rhi::Device& device() { return m_app->device(); }
    /// The output texture as a frame target, for Renderer::render().
    [[nodiscard]] const renderer::FrameTarget& output() const noexcept { return m_output; }

    /// Runs one frame: `record` records into the frame's command list (rendering into output()), then the
    /// output is copied back and returned once the GPU is done.
    [[nodiscard]] Image render(const std::function<void(rhi::CommandList&)>& record);

private:
    std::unique_ptr<engine::App> m_app;
    renderer::FrameTarget m_output{};
    rhi::BufferHandle m_readback{};
};

} // namespace teserra::snapshot
