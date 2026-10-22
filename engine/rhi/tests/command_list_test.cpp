// Command recording: the stream layout of spike #15, reading records back as backends do, steady-state
// recording without heap allocations, and the recording rules. Every list here is created on its own,
// without a device: recording makes no backend calls.

#include <teserra/rhi/command_list.hpp>

#include <teserra/rhi/limits.hpp>
#include <teserra/test/allocation_counter.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <iterator>
#include <string>
#include <vector>

using namespace teserra;
using namespace teserra::rhi;

namespace {

constexpr TextureHandle kBackbuffer{1, 1};
constexpr GraphicsPipelineHandle kPipeline{2, 1};
constexpr BindGroupHandle kGroup{3, 1};
constexpr BufferHandle kVertices{4, 1};
constexpr BufferHandle kIndices{5, 1};
constexpr BufferHandle kStaging{6, 1};
constexpr TextureHandle kAtlas{7, 1};
constexpr BufferHandle kReadback{8, 1};
constexpr TextureHandle kDepth{9, 1};

constexpr RenderPassDesc kClearPass{
    .colorAttachments = {{.texture = kBackbuffer,
                          .loadOp = LoadOp::Clear,
                          .clearColor = {0.1F, 0.2F, 0.3F, 1.0F}}},
};

static_assert(std::forward_iterator<CommandIterator>);
static_assert(sizeof(DrawCommand) == 16);
static_assert(sizeof(BindVertexBuffersCommand) == 8);
static_assert(sizeof(VertexBufferBinding) == 16);
static_assert(commandTrailingOffset(0) == 8);
static_assert(commandTrailingOffset(4) == 16);

// One frame with every kind of command.
void recordFrame(CommandList& cmd) {
    const std::array dynamicOffsets{256U};
    cmd.pushDebugGroup("Main pass");
    cmd.beginRenderPass(kClearPass);
    cmd.bindPipeline(kPipeline);
    cmd.bindGroup(0, kGroup, dynamicOffsets);
    cmd.bindVertexBuffer(0, kVertices);
    cmd.bindIndexBuffer(kIndices, IndexType::Uint16);
    cmd.setViewport({.width = 1280.0F, .height = 720.0F});
    cmd.setScissor({.width = 1280, .height = 720});
    cmd.drawIndexed({.indexCount = 6});
    cmd.draw({.vertexCount = 3});
    cmd.endRenderPass();
    cmd.popDebugGroup();
    cmd.copyBuffer({.source = kStaging, .destination = kVertices, .destinationOffset = 64, .size = 128});
    cmd.copyBufferToTexture({.buffer = kStaging,
                             .bufferOffset = 256,
                             .bytesPerRow = 1024,
                             .texture = kAtlas,
                             .width = 256,
                             .height = 256});
    cmd.copyTextureToBuffer(
        {.buffer = kReadback, .texture = kBackbuffer, .x = 16, .y = 32, .width = 64, .height = 64});
}

std::vector<CommandType> typesOf(const CommandList& list) {
    std::vector<CommandType> types;
    for (const Command command : list.commands()) {
        types.push_back(command.type());
    }
    return types;
}

// A batch of sprites, as the renderer records it: ~1000 commands.
void recordSprites(CommandList& cmd) {
    cmd.pushDebugGroup("Sprites");
    cmd.beginRenderPass(kClearPass);
    for (u32 batch = 0; batch < 250; ++batch) {
        if (batch % 50 == 0) {
            cmd.bindPipeline(GraphicsPipelineHandle{batch / 50, 1});
        }
        const std::array offsets{batch * 256};
        cmd.bindGroup(3, kGroup, offsets);
        const std::array buffers{
            VertexBufferBinding{.buffer = kVertices, .offset = u64{batch} * 4096},
            VertexBufferBinding{.buffer = kIndices, .offset = u64{batch} * 64},
        };
        cmd.bindVertexBuffers(0, buffers);
        cmd.draw({.vertexCount = 6, .firstVertex = batch * 6});
    }
    cmd.endRenderPass();
    cmd.popDebugGroup();
}

} // namespace

TEST_CASE("CommandList: commands read back in recording order", "[rhi][command_list]") {
    CommandList cmd;
    recordFrame(cmd);

    CHECK(cmd.commandCount() == 15);
    CHECK(typesOf(cmd) == std::vector{
                              CommandType::PushDebugGroup,
                              CommandType::BeginRenderPass,
                              CommandType::BindPipeline,
                              CommandType::BindGroup,
                              CommandType::BindVertexBuffers,
                              CommandType::BindIndexBuffer,
                              CommandType::SetViewport,
                              CommandType::SetScissor,
                              CommandType::DrawIndexed,
                              CommandType::Draw,
                              CommandType::EndRenderPass,
                              CommandType::PopDebugGroup,
                              CommandType::CopyBuffer,
                              CommandType::CopyBufferToTexture,
                              CommandType::CopyTextureToBuffer,
                          });
}

TEST_CASE("CommandList: payloads and trailing arrays read back in place", "[rhi][command_list]") {
    CommandList cmd;
    recordFrame(cmd);
    const std::vector<Command> commands(cmd.commands().begin(), cmd.commands().end());
    REQUIRE(commands.size() == 15);

    CHECK(commands[0].debugLabel() == "Main pass");

    const RenderPassDesc& pass = commands[1].as<BeginRenderPassCommand>().pass;
    CHECK(pass.colorAttachments[0].texture == kBackbuffer);
    CHECK(pass.colorAttachments[0].loadOp == LoadOp::Clear);
    CHECK(pass.colorAttachments[0].clearColor.z == 0.3F);
    CHECK(pass.colorAttachments[1].texture.isNull());

    CHECK(commands[2].as<BindPipelineCommand>().pipeline == kPipeline);

    const auto& bindGroup = commands[3].as<BindGroupCommand>();
    CHECK(bindGroup.index == 0);
    CHECK(bindGroup.group == kGroup);
    REQUIRE(commands[3].dynamicOffsets().size() == 1);
    CHECK(commands[3].dynamicOffsets()[0] == 256);

    CHECK(commands[4].as<BindVertexBuffersCommand>().firstSlot == 0);
    REQUIRE(commands[4].vertexBuffers().size() == 1);
    CHECK(commands[4].vertexBuffers()[0].buffer == kVertices);
    CHECK(commands[4].vertexBuffers()[0].offset == 0);

    const auto& bindIndices = commands[5].as<BindIndexBufferCommand>();
    CHECK(bindIndices.buffer == kIndices);
    CHECK(bindIndices.indexType == IndexType::Uint16);

    CHECK(commands[6].as<SetViewportCommand>().viewport.width == 1280.0F);
    CHECK(commands[6].as<SetViewportCommand>().viewport.maxDepth == 1.0F);
    CHECK(commands[7].as<SetScissorCommand>().scissor.height == 720);
    CHECK(commands[8].as<DrawIndexedCommand>().args.indexCount == 6);
    CHECK(commands[9].as<DrawCommand>().args.vertexCount == 3);
    CHECK(commands[9].as<DrawCommand>().args.instanceCount == 1);

    const BufferCopy& bufferCopy = commands[12].as<CopyBufferCommand>().copy;
    CHECK(bufferCopy.destination == kVertices);
    CHECK(bufferCopy.destinationOffset == 64);
    CHECK(bufferCopy.size == 128);
    CHECK(commands[13].as<CopyBufferToTextureCommand>().copy.bytesPerRow == 1024);
    CHECK(commands[14].as<CopyTextureToBufferCommand>().copy.y == 32);
}

TEST_CASE("CommandList: several vertex buffers and dynamic offsets", "[rhi][command_list]") {
    CommandList cmd;
    const std::array buffers{
        VertexBufferBinding{.buffer = kVertices, .offset = 16},
        VertexBufferBinding{.buffer = kStaging, .offset = 32},
        VertexBufferBinding{.buffer = kReadback, .offset = 48},
    };
    const std::array offsets{0U, 256U, 512U};

    cmd.beginRenderPass(kClearPass);
    cmd.bindVertexBuffers(2, buffers);
    cmd.bindGroup(1, kGroup, offsets);
    cmd.bindGroup(2, kGroup);
    cmd.endRenderPass();

    auto command = cmd.commands().begin();
    ++command;
    CHECK((*command).as<BindVertexBuffersCommand>().firstSlot == 2);
    const std::span<const VertexBufferBinding> bindings = (*command).vertexBuffers();
    REQUIRE(bindings.size() == 3);
    CHECK(bindings[2].buffer == kReadback);
    CHECK(bindings[2].offset == 48);

    ++command;
    const std::span<const u32> dynamicOffsets = (*command).dynamicOffsets();
    CHECK(std::vector(dynamicOffsets.begin(), dynamicOffsets.end()) == std::vector{0U, 256U, 512U});

    ++command;
    CHECK((*command).dynamicOffsets().empty());
}

TEST_CASE("CommandList: records are 8-byte aligned, with an 8-byte header", "[rhi][command_list]") {
    CommandList cmd;
    recordFrame(cmd);

    usize total = 0;
    for (const Command command : cmd.commands()) {
        CHECK(command.size() % kCommandAlignment == 0);
        total += command.size();
    }
    CHECK(total == cmd.sizeInBytes());

    std::vector<u32> sizes;
    for (const Command command : cmd.commands()) {
        sizes.push_back(command.size());
    }
    CHECK(sizes[0] == 32); // PushDebugGroup: header, u32 length, 9 label bytes at +16, padding
    CHECK(sizes[4] == 32); // BindVertexBuffers: header, 8-byte payload, one 16-byte binding
    CHECK(sizes[9] == 24); // Draw: header and 16-byte payload
    CHECK(sizes[10] == 8); // EndRenderPass: header only
    CHECK(sizes[11] == 8); // PopDebugGroup: header only
}

TEST_CASE("CommandList: an empty debug label", "[rhi][command_list]") {
    CommandList cmd;
    cmd.pushDebugGroup("");
    cmd.popDebugGroup();

    CHECK((*cmd.commands().begin()).debugLabel().empty());
    CHECK((*cmd.commands().begin()).size() == 16);
}

TEST_CASE("CommandList: reset empties the list and keeps its capacity", "[rhi][command_list]") {
    CommandList cmd;
    CHECK(cmd.empty());
    CHECK(cmd.capacityInBytes() == 0);
    recordFrame(cmd);
    cmd.finish();
    const usize capacity = cmd.capacityInBytes();
    CHECK(capacity >= cmd.sizeInBytes());

    cmd.reset();

    CHECK(cmd.empty());
    CHECK(cmd.commandCount() == 0);
    CHECK(cmd.sizeInBytes() == 0);
    CHECK_FALSE(cmd.isFinished());
    CHECK(cmd.commands().begin() == cmd.commands().end());
    CHECK(cmd.capacityInBytes() == capacity);

    recordFrame(cmd); // records again after reset()
    CHECK(cmd.commandCount() == 15);
}

TEST_CASE("CommandList: the stream grows beyond its initial capacity", "[rhi][command_list]") {
    CommandList cmd;
    recordSprites(cmd);

    CHECK(cmd.commandCount() == 759);
    CHECK(cmd.capacityInBytes() >= cmd.sizeInBytes());
    usize draws = 0;
    for (const Command command : cmd.commands()) {
        if (command.type() == CommandType::Draw) {
            CHECK(command.as<DrawCommand>().args.firstVertex == draws * 6);
            ++draws;
        }
    }
    CHECK(draws == 250);
}

TEST_CASE("CommandList: steady-state recording does not allocate", "[rhi][command_list][allocation]") {
    CommandList cmd;
    recordSprites(cmd); // warm-up: the stream grows to a frame's size
    cmd.finish();
    cmd.reset();

    u64 allocations = 0;
    {
        const test::AllocationCounter counter;
        recordSprites(cmd);
        cmd.finish();
        allocations = counter.count();
    }

    CHECK(allocations == 0);
    CHECK(cmd.commandCount() == 759);
}

TEST_CASE("describeCommands: one line per command, indented by nesting", "[rhi][command_list]") {
    CommandList cmd;
    recordFrame(cmd);
    cmd.beginRenderPass({.depthStencil = {.texture = kDepth}});
    cmd.endRenderPass();

    CHECK(
        describeCommands(cmd) ==
        "PushDebugGroup \"Main pass\"\n"
        "  BeginRenderPass color0=1:1 load=Clear store=Store clear=(0.1, 0.2, 0.3, 1)\n"
        "    BindPipeline pipeline=2:1\n"
        "    BindGroup index=0 group=3:1 dynamicOffsets=[256]\n"
        "    BindVertexBuffers firstSlot=0 buffers=[4:1+0]\n"
        "    BindIndexBuffer buffer=5:1+0 indexType=Uint16\n"
        "    SetViewport x=0 y=0 width=1280 height=720 minDepth=0 maxDepth=1\n"
        "    SetScissor x=0 y=0 width=1280 height=720\n"
        "    DrawIndexed indexCount=6 instanceCount=1 firstIndex=0 vertexOffset=0 firstInstance=0\n"
        "    Draw vertexCount=3 instanceCount=1 firstVertex=0 firstInstance=0\n"
        "  EndRenderPass\n"
        "PopDebugGroup\n"
        "CopyBuffer source=6:1+0 destination=4:1+64 size=128\n"
        "CopyBufferToTexture buffer=6:1+256 bytesPerRow=1024 texture=7:1 mip=0 layers=0+1 region=(0, 0) "
        "256x256\n"
        "CopyTextureToBuffer buffer=8:1+0 bytesPerRow=0 texture=1:1 mip=0 layers=0+1 region=(16, 32) 64x64\n"
        "BeginRenderPass depth=9:1 load=Clear store=DontCare clear=1\n"
        "EndRenderPass\n");
}

TEST_CASE("commandTypeName: names every command type", "[rhi][command_list]") {
    for (usize i = 0; i < kCommandTypeCount; ++i) {
        CHECK(commandTypeName(static_cast<CommandType>(i)) != "Unknown");
    }
    CHECK(commandTypeName(CommandType::BindVertexBuffers) == "BindVertexBuffers");
}

#if TESERRA_ENABLE_ASSERTS

namespace {

std::string failureOf(auto&& record) {
    return test::requireAssert(record).message();
}

} // namespace

TEST_CASE("CommandList validation: draws need a render pass and a pipeline",
          "[rhi][command_list][validation]") {
    CommandList cmd;
    CHECK(failureOf([&] { cmd.draw({.vertexCount = 3}); }) == "draw outside a render pass");

    cmd.beginRenderPass(kClearPass);
    CHECK(failureOf([&] { cmd.draw({.vertexCount = 3}); }) ==
          "draw without a pipeline bound in this render pass");
    cmd.bindPipeline(kPipeline);
    CHECK(failureOf([&] { cmd.drawIndexed({.indexCount = 3}); }) ==
          "drawIndexed without an index buffer bound in this render pass");
    cmd.draw({.vertexCount = 3});
    cmd.endRenderPass();

    // Bindings do not carry over into the next render pass.
    cmd.beginRenderPass(kClearPass);
    CHECK(failureOf([&] { cmd.draw({.vertexCount = 3}); }) ==
          "draw without a pipeline bound in this render pass");
}

TEST_CASE("CommandList validation: render passes", "[rhi][command_list][validation]") {
    CommandList cmd;
    CHECK(failureOf([&] { cmd.endRenderPass(); }) == "endRenderPass outside a render pass");
    CHECK(failureOf([&] { cmd.beginRenderPass({}); }) == "beginRenderPass without attachments");

    RenderPassDesc gap = kClearPass;
    gap.colorAttachments[2].texture = kAtlas;
    CHECK(failureOf([&] { cmd.beginRenderPass(gap); }) ==
          "Color attachment 2 follows the unused attachment 1: attachments in use must be the leading ones");

    cmd.beginRenderPass(kClearPass);
    CHECK(failureOf([&] { cmd.beginRenderPass(kClearPass); }) ==
          "beginRenderPass inside a render pass: render passes do not nest");
}

TEST_CASE("CommandList validation: bindings belong in a render pass", "[rhi][command_list][validation]") {
    CommandList cmd;
    CHECK(failureOf([&] { cmd.bindPipeline(kPipeline); }) == "bindPipeline outside a render pass");
    CHECK(failureOf([&] { cmd.bindGroup(0, kGroup); }) == "bindGroup outside a render pass");
    CHECK(failureOf([&] { cmd.bindVertexBuffer(0, kVertices); }) ==
          "bindVertexBuffers outside a render pass");
    CHECK(failureOf([&] { cmd.bindIndexBuffer(kIndices, IndexType::Uint32); }) ==
          "bindIndexBuffer outside a render pass");
    CHECK(failureOf([&] { cmd.setViewport({.width = 1, .height = 1}); }) ==
          "setViewport outside a render pass");
    CHECK(failureOf([&] { cmd.setScissor({.width = 1, .height = 1}); }) ==
          "setScissor outside a render pass");
}

TEST_CASE("CommandList validation: binding arguments", "[rhi][command_list][validation]") {
    CommandList cmd;
    cmd.beginRenderPass(kClearPass);

    CHECK(failureOf([&] { cmd.bindPipeline({}); }) == "bindPipeline with a null pipeline");
    CHECK(failureOf([&] { cmd.bindGroup(kMaxBindGroups, kGroup); }) ==
          "bindGroup at index 4; the limit is 4 bind groups");
    CHECK(failureOf([&] { cmd.bindGroup(1, {}); }) == "bindGroup with a null bind group at index 1");
    const std::array<u32, kMaxDynamicOffsets + 1> tooManyOffsets{};
    CHECK(failureOf([&] { cmd.bindGroup(0, kGroup, tooManyOffsets); }) ==
          "bindGroup with 9 dynamic offsets; the limit is 8");

    const std::array<VertexBufferBinding, 2> two{{{.buffer = kVertices}, {.buffer = kVertices}}};
    CHECK(failureOf([&] { cmd.bindVertexBuffers(kMaxVertexBuffers - 1, two); }) ==
          "bindVertexBuffers to slots 7 to 8; the limit is 8 vertex buffers");
    CHECK(failureOf([&] { cmd.bindVertexBuffer(3, {}); }) ==
          "bindVertexBuffers with a null buffer for slot 3");
    CHECK(failureOf([&] { cmd.bindIndexBuffer(kIndices, IndexType::Uint32, 6); }) ==
          "bindIndexBuffer offset 6 is not a multiple of the index size 4");

    CHECK(failureOf([&] { cmd.setViewport({.width = 0, .height = 720}); }) ==
          "setViewport with an empty viewport (0x720)");
    CHECK(failureOf([&] { cmd.setViewport({.width = 1, .height = 1, .maxDepth = 2}); }) ==
          "setViewport depth range [0, 2] is outside [0, 1]");
    CHECK(failureOf([&] { cmd.setScissor({.width = 16}); }) == "setScissor with an empty rectangle (16x0)");

    // A failed check records nothing.
    CHECK(cmd.commandCount() == 1);
}

TEST_CASE("CommandList validation: copies happen outside render passes", "[rhi][command_list][validation]") {
    CommandList cmd;
    CHECK(failureOf([&] { cmd.copyBuffer({.source = kStaging, .destination = kVertices}); }) ==
          "copyBuffer of 0 bytes");
    CHECK(failureOf([&] {
              cmd.copyBuffer(
                  {.source = kStaging, .destination = kStaging, .destinationOffset = 8, .size = 16});
          }) == "copyBuffer between overlapping ranges of buffer 6:1");
    cmd.copyBuffer({.source = kStaging, .destination = kStaging, .destinationOffset = 16, .size = 16});
    CHECK(failureOf([&] { cmd.copyBufferToTexture({.buffer = kStaging, .width = 4, .height = 4}); }) ==
          "copyBufferToTexture with a null resource");
    CHECK(failureOf([&] { cmd.copyTextureToBuffer({.buffer = kReadback, .texture = kAtlas, .width = 4}); }) ==
          "copyTextureToBuffer of an empty region (4x0, 1 layers)");

    cmd.beginRenderPass(kClearPass);
    CHECK(failureOf([&] { cmd.copyBuffer({.source = kStaging, .destination = kVertices, .size = 4}); }) ==
          "copyBuffer inside a render pass: copies happen outside render passes");
    CHECK(failureOf([&] {
              cmd.copyBufferToTexture({.buffer = kStaging, .texture = kAtlas, .width = 4, .height = 4});
          }) == "copyBufferToTexture inside a render pass: copies happen outside render passes");
    CHECK(failureOf([&] {
              cmd.copyTextureToBuffer({.buffer = kReadback, .texture = kAtlas, .width = 4, .height = 4});
          }) == "copyTextureToBuffer inside a render pass: copies happen outside render passes");
}

TEST_CASE("CommandList validation: debug groups", "[rhi][command_list][validation]") {
    CommandList cmd;
    CHECK(failureOf([&] { cmd.popDebugGroup(); }) == "popDebugGroup without a matching pushDebugGroup");
    const std::string tooLong(kMaxDebugLabelLength + 1, 'x');
    CHECK(failureOf([&] { cmd.pushDebugGroup(tooLong); }) ==
          "pushDebugGroup label of 256 bytes; the limit is 255");
    cmd.pushDebugGroup(std::string(kMaxDebugLabelLength, 'x'));

    // A group opened before a render pass cannot be closed inside it, and one opened inside it cannot stay
    // open after it.
    cmd.beginRenderPass(kClearPass);
    CHECK(failureOf([&] { cmd.popDebugGroup(); }) ==
          "popDebugGroup inside a render pass closes a debug group opened before the pass");
    cmd.pushDebugGroup("Inside");
    CHECK(failureOf([&] { cmd.endRenderPass(); }) ==
          "endRenderPass with 1 debug group(s) of the render pass still open");
    cmd.popDebugGroup();
    cmd.endRenderPass();
    cmd.popDebugGroup();
}

TEST_CASE("CommandList validation: finishing", "[rhi][command_list][validation]") {
    CommandList cmd;
    cmd.pushDebugGroup("Frame");
    CHECK(failureOf([&] { cmd.finish(); }) == "Finishing a command list with 1 open debug group(s)");
    cmd.beginRenderPass(kClearPass);
    CHECK(failureOf([&] { cmd.finish(); }) == "Finishing a command list with an open render pass");
    cmd.endRenderPass();
    cmd.popDebugGroup();
    cmd.finish();

    CHECK(failureOf([&] { cmd.pushDebugGroup("Late"); }) ==
          "Recording into a finished command list; reset() it first");
    CHECK(failureOf([&] { cmd.beginRenderPass(kClearPass); }) ==
          "Recording into a finished command list; reset() it first");
    CHECK(failureOf([&] { cmd.finish(); }) == "Recording into a finished command list; reset() it first");

    cmd.reset();
    cmd.pushDebugGroup("Next frame");
    CHECK(cmd.commandCount() == 1);
}

TEST_CASE("Command: reading a record as the wrong payload asserts", "[rhi][command_list][validation]") {
    CommandList cmd;
    cmd.beginRenderPass(kClearPass);
    const Command command = *cmd.commands().begin();

    CHECK(failureOf([&] { static_cast<void>(command.as<DrawCommand>()); }) ==
          "Reading a BeginRenderPass command as Draw");
    CHECK(failureOf([&] { static_cast<void>(command.debugLabel()); }) ==
          "Reading a BeginRenderPass command as PushDebugGroup");
}

#endif
