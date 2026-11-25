// Materials on the null device: the parameter layout comes from the reflection of set 2, parameters are
// written by type at their std140 offsets, materials and instances load from JSON files with shaders and
// textures as dependencies, and sprites draw with a material instance through cached pipelines, creating
// and allocating nothing after warm-up.

#include <teserra/renderer/material.hpp>

#include <teserra/assets/asset_registry.hpp>
#include <teserra/assets/mount_table.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/renderer/renderer.hpp>
#include <teserra/renderer/sprite_pass.hpp>
#include <teserra/rhi/commands.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/test/allocation_counter.hpp>
#include <teserra/test/log_capture.hpp>
#include <teserra/test/memory_file_system.hpp>
#include <teserra/test/png.hpp>

#include <catch2/catch_test_macros.hpp>
#include <renderer_tests/shaders.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace teserra;
using namespace teserra::renderer;

namespace {

constexpr std::array<u8, 4> kWhite{255, 255, 255, 255};

f32 floatAt(std::span<const std::byte> bytes, usize offset) {
    f32 value = 0.0F;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

std::vector<std::byte> blob(std::span<const std::byte> embedded) {
    return {embedded.begin(), embedded.end()};
}

struct Fixture {
    Fixture() {
        REQUIRE(mounts.mount("assets", "/game/assets"));
        files.addFile("/game/assets/shaders/glow.vert.tshader", blob(test::shaders::kGlowVert));
        files.addFile("/game/assets/shaders/glow.frag.tshader", blob(test::shaders::kGlowFrag));
        files.addFile("/game/assets/noise.png", test::writePng(1, 1, test::PngColor::Rgba, kWhite));
        files.addTextFile("/game/assets/glow.material.json", R"({
            "vertex": "shaders/glow.vert.tshader",
            "fragment": "shaders/glow.frag.tshader",
            "blend": "additive",
            "filter": "nearest",
            "parameters": {"tint": [1, 0.5, 0.25, 1], "strength": 2},
            "textures": {"noise": "noise.png"}
        })");
        files.addTextFile("/game/assets/red.material-instance.json", R"({
            "material": "glow.material.json",
            "parameters": {"tint": [1, 0, 0, 1], "offset": [3, 4]}
        })");
        REQUIRE(registerAssetLoaders(registry, *device));
    }

    std::unique_ptr<rhi::null::NullDevice> device =
        rhi::null::createHeadlessDevice({.enableValidation = true});
    test::MemoryFileSystem files;
    assets::MountTable mounts{files};
    assets::AssetRegistry registry{{.mounts = &mounts}};
};

// The parameter buffer of an instance, from its bind group.
std::span<const std::byte> instanceBuffer(const rhi::null::NullDevice& device,
                                          const MaterialInstance& instance) {
    const rhi::BindGroupDesc* group = device.bindGroup(instance.group());
    REQUIRE(group != nullptr);
    REQUIRE_FALSE(group->entries.empty());
    return device.bufferData(group->entries[0].buffer.buffer);
}

} // namespace

TEST_CASE("Materials: the parameter layout comes from the reflection of set 2", "[renderer][material]") {
    const rhi::ShaderReflection vertex{
        .bindings = {{.group = 0,
                      .binding = 0,
                      .type = rhi::BindingType::UniformBuffer,
                      .size = 288,
                      .name = "Camera"},
                     {.group = 2,
                      .binding = 0,
                      .type = rhi::BindingType::UniformBuffer,
                      .size = 32,
                      .name = "Glow",
                      .members = {{.name = "tint", .offset = 0, .size = 16, .rows = 4},
                                  {.name = "strength", .offset = 16, .size = 4}}}}};
    const rhi::ShaderReflection fragment{
        .bindings = {
            vertex.bindings[1],
            {.group = 2, .binding = 3, .type = rhi::BindingType::SampledTexture, .name = "mask"},
            {.group = 2, .binding = 1, .type = rhi::BindingType::SampledTexture, .name = "noise"},
            {.group = 3, .binding = 0, .type = rhi::BindingType::SampledTexture, .name = "spriteTexture"}}};

    SECTION("both stages, merged") {
        const std::array stages{&vertex, &fragment};
        const Result<MaterialLayout> layout = MaterialLayout::fromReflection(stages);
        REQUIRE(layout.has_value());
        CHECK(layout->blockBinding == 0U);
        CHECK(layout->blockSize == 32);
        REQUIRE(layout->parameters.size() == 2);
        CHECK(layout->parameter("strength")->offset == 16);
        CHECK(layout->parameter("missing") == nullptr);
        REQUIRE(layout->textures.size() == 2); // set 3 is not the material's
        CHECK(layout->textures[0].name == "noise");
        CHECK(layout->textures[1].binding == 3);
        CHECK(layout->visibility == (rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment));
    }
    SECTION("a material without set 2 has an empty layout") {
        const rhi::ShaderReflection plain{};
        const std::array stages{&plain, &plain};
        const Result<MaterialLayout> layout = MaterialLayout::fromReflection(stages);
        REQUIRE(layout.has_value());
        CHECK(layout->empty());
    }
    SECTION("two blocks, storage buffers and disagreeing stages are refused") {
        rhi::ShaderReflection twoBlocks = fragment;
        twoBlocks.bindings.push_back(
            {.group = 2, .binding = 5, .type = rhi::BindingType::UniformBuffer, .size = 16, .name = "More"});
        const std::array<const rhi::ShaderReflection*, 2> a{&vertex, &twoBlocks};
        CHECK(MaterialLayout::fromReflection(a).error().message.starts_with("Set 2 has two uniform blocks"));

        rhi::ShaderReflection storage{};
        storage.bindings.push_back(
            {.group = 2, .binding = 2, .type = rhi::BindingType::StorageBuffer, .name = "Data"});
        const std::array<const rhi::ShaderReflection*, 2> b{&vertex, &storage};
        CHECK(MaterialLayout::fromReflection(b).error().message.starts_with("Set 2 has a storage buffer"));

        rhi::ShaderReflection other = vertex;
        other.bindings[1].size = 48;
        const std::array<const rhi::ShaderReflection*, 2> c{&vertex, &other};
        CHECK(MaterialLayout::fromReflection(c).error().message.starts_with(
            "The shaders disagree about set 2"));
    }
}

TEST_CASE("Materials: parameters are written by type at their offsets", "[renderer][material]") {
    std::vector<std::byte> block(96);
    const MaterialParameter tint{.name = "tint", .offset = 16, .size = 16, .rows = 4};
    const MaterialParameter count{
        .name = "count", .offset = 0, .size = 4, .scalarType = rhi::ShaderScalarType::Int};
    const MaterialParameter model{.name = "model", .offset = 32, .size = 64, .rows = 4, .columns = 4};

    CHECK(writeParameter(tint, block, Color{.r = 1.0F, .g = 0.5F, .b = 0.25F, .a = 1.0F}));
    CHECK(floatAt(block, 20) == 0.5F);
    CHECK(writeParameter(count, block, i32{7}));
    i32 written = 0;
    std::memcpy(&written, block.data(), sizeof(written));
    CHECK(written == 7);
    Mat4 matrix{1.0F};
    matrix[3][0] = 5.0F; // a translation, column 3
    CHECK(writeParameter(model, block, matrix));
    CHECK(floatAt(block, 32 + 48) == 5.0F);

    CHECK_FALSE(writeParameter(tint, block, 1.0F));       // a vec4 is not a float
    CHECK_FALSE(writeParameter(count, block, u32{1}));    // an int is not a uint
    CHECK_FALSE(writeParameter(tint, block, Vec3{1.0F})); // nor a vec3
    std::vector<std::byte> small(8);
    CHECK_FALSE(writeParameter(tint, small, Vec4{1.0F})); // outside the block
}

TEST_CASE("Materials: materials and instances load from JSON files", "[renderer][material][assets]") {
    Fixture fixture;
    const assets::Asset<Material> glow = fixture.registry.load<Material>("assets://glow.material.json");
    REQUIRE(glow.isReady());
    REQUIRE_FALSE(glow.isFallback());
    const Material& material = glow.get();
    CHECK(material.state().blend == BlendMode::Additive);
    CHECK(material.layout().blockSize == 32);
    REQUIRE(material.layout().parameter("offset") != nullptr);
    CHECK(material.layout().parameter("offset")->offset == 24); // std140: a vec2 aligns to 8
    CHECK(floatAt(material.defaults(), 4) == 0.5F);             // tint.g
    CHECK(floatAt(material.defaults(), 16) == 2.0F);            // strength
    REQUIRE(material.defaultTextures().size() == 1);
    CHECK_FALSE(material.defaultTextures()[0].isNull());
    CHECK(fixture.device->sampler(material.sampler())->magFilter == rhi::Filter::Nearest);

    const assets::Asset<MaterialInstance> red =
        fixture.registry.load<MaterialInstance>("assets://red.material-instance.json");
    REQUIRE(red.isReady());
    const MaterialInstance& instance = red.get();
    CHECK(&instance.material() == &material);     // the same material asset
    CHECK(floatAt(instance.block(), 4) == 0.0F);  // tint.g overridden
    CHECK(floatAt(instance.block(), 16) == 2.0F); // strength from the material
    CHECK(floatAt(instance.block(), 28) == 4.0F); // offset.y
    REQUIRE_FALSE(instance.group().isNull());

    instance.flush();
    CHECK(floatAt(instanceBuffer(*fixture.device, instance), 0) == 1.0F);
    CHECK(floatAt(instanceBuffer(*fixture.device, instance), 24) == 3.0F);
}

TEST_CASE("Materials: JSON errors name the field", "[renderer][material][assets]") {
    Fixture fixture;
    // The logged failure of loading `json` as a material.
    const auto failure = [&](std::string_view json) {
        fixture.files.addTextFile("/game/assets/bad.material.json", std::string(json));
        const test::LogCapture logs;
        {
            const assets::Asset<Material> bad = fixture.registry.load<Material>("assets://bad.material.json");
            CHECK_FALSE(bad.isReady());
        }
        fixture.registry.endFrame(); // unloads the failed asset, so the next load reads the file again
        std::string messages;
        for (const test::CapturedLog& record : logs.records()) {
            messages += record.message;
        }
        return messages;
    };
    CHECK(failure(R"({"fragment": "shaders/glow.frag.tshader"})").find("'vertex' must be a path") !=
          std::string::npos);
    CHECK(failure(R"({"vertex": "shaders/glow.vert.tshader", "fragment": "shaders/glow.frag.tshader",
                      "blend": "multiply"})")
              .find("'blend' must be") != std::string::npos);
    CHECK(failure(R"({"vertex": "shaders/glow.vert.tshader", "fragment": "shaders/glow.frag.tshader",
                      "parameters": {"glow": 1}})")
              .find("no parameter 'glow'") != std::string::npos);
    CHECK(failure(R"({"vertex": "shaders/glow.vert.tshader", "fragment": "shaders/glow.frag.tshader",
                      "parameters": {"tint": [1, 2]}})")
              .find("'tint' takes 4 numbers") != std::string::npos);
    CHECK(failure(R"({"vertex": "shaders/glow.vert.tshader", "fragment": "shaders/glow.frag.tshader",
                      "textures": {"albedo": "noise.png"}})")
              .find("no texture slot 'albedo'") != std::string::npos);
}

TEST_CASE("Materials: instances made in code change parameters and textures", "[renderer][material]") {
    Fixture fixture;
    const assets::Asset<Material> glow = fixture.registry.load<Material>("assets://glow.material.json");
    REQUIRE(glow.isReady());
    MaterialInstance instance = *MaterialInstance::create(*fixture.device, glow);
    CHECK(instance.set("strength", 0.5F));
    CHECK_FALSE(instance.set("strength", Vec2{1.0F}));
    CHECK_FALSE(instance.set("missing", 1.0F));
    CHECK(floatAt(instance.block(), 16) == 0.5F);
    instance.flush();
    CHECK(floatAt(instanceBuffer(*fixture.device, instance), 16) == 0.5F);

    const rhi::BindGroupHandle before = instance.group();
    const assets::Asset<Texture> other = fixture.registry.load<Texture>("assets://noise.png");
    CHECK(instance.setTexture("noise", other));
    CHECK(instance.group() != before); // a new group for the new texture
    CHECK_FALSE(instance.setTexture("albedo", other));
}

TEST_CASE("Materials: sprites draw with a material instance", "[renderer][material][sprites]") {
    Fixture fixture;
    const assets::Asset<MaterialInstance> red =
        fixture.registry.load<MaterialInstance>("assets://red.material-instance.json");
    REQUIRE(red.isReady());
    const assets::Asset<Material> glow = fixture.registry.load<Material>("assets://glow.material.json");
    MaterialInstance blue = *MaterialInstance::create(*fixture.device, glow);
    REQUIRE(blue.set("tint", Vec4{0.0F, 0.0F, 1.0F, 1.0F}));

    auto renderer = *Renderer::create(*fixture.device);
    auto created = SpritePass::create(*fixture.device, renderer->frameGroupLayout());
    REQUIRE(created.has_value());
    SpritePass* sprites = created->get();
    renderer->addPass(std::move(created).value());
    const rhi::TextureHandle output =
        *fixture.device->createTexture({.format = rhi::Format::Rgba8Srgb,
                                        .width = 64,
                                        .height = 64,
                                        .usage = rhi::TextureUsage::RenderTarget});
    const rhi::TextureHandle texture = *fixture.device->createTexture({.width = 4, .height = 4});
    FramePacket packet;
    packet.views.emplace_back();
    packet.sprites = {{.texture = texture, .material = &red.get()},
                      {.texture = texture, .material = &red.get()},
                      {.texture = texture, .material = &blue},
                      {.texture = texture}};

    const auto frame = [&] {
        fixture.device->beginFrame();
        rhi::CommandList& commands = fixture.device->acquireCommandList();
        renderer->render(packet, {.texture = output, .format = rhi::Format::Rgba8Srgb, .size = {64, 64}},
                         commands);
        fixture.device->submit(commands);
        std::string described = rhi::describeCommands(*fixture.device->submitted()[0]);
        fixture.device->endFrame();
        return described.substr(0, described.find("PushDebugGroup \"Composite\""));
    };
    const std::string commands = frame();
    usize draws = 0;
    usize materialGroups = 0;
    usize drawGroups = 0;
    std::istringstream lines(commands);
    for (std::string line; std::getline(lines, line);) {
        line = line.substr(line.find_first_not_of(' '));
        draws += line.starts_with("DrawIndexed") ? 1 : 0;
        materialGroups += line.starts_with("BindGroup index=2") ? 1 : 0;
        drawGroups += line.starts_with("BindGroup index=3") ? 1 : 0;
    }
    // The two red sprites batch; blue and the plain sprite draw on their own.
    CHECK(draws == 3);
    CHECK(materialGroups == 3); // red, blue, and sprite_unlit's texture group
    CHECK(drawGroups == 1);     // the sprite texture at set 3 stays bound across both instances
    CHECK(sprites->stats().drawCalls == 3);
    // The instance's parameters reached its buffer before the draw.
    CHECK(floatAt(instanceBuffer(*fixture.device, blue), 8) == 1.0F);

    SECTION("steady-state frames create nothing and allocate nothing") {
        static_cast<void>(frame());
        static_cast<void>(frame());
        const usize objects = fixture.device->liveObjectCount();
        u64 allocations = 0;
        {
            const test::AllocationCounter counter;
            fixture.device->beginFrame();
            rhi::CommandList& list = fixture.device->acquireCommandList();
            renderer->render(packet, {.texture = output, .format = rhi::Format::Rgba8Srgb, .size = {64, 64}},
                             list);
            fixture.device->submit(list);
            fixture.device->endFrame();
            allocations = counter.count();
        }
        CHECK(allocations == 0);
        CHECK(fixture.device->liveObjectCount() == objects);
    }
    SECTION("an instance with an empty texture slot is left out") {
        Material::Desc desc{.vertex = fixture.registry.load<Shader>("assets://shaders/glow.vert.tshader"),
                            .fragment = fixture.registry.load<Shader>("assets://shaders/glow.frag.tshader"),
                            .debugName = "Bare"};
        const assets::Asset<Material> bare = fixture.registry.load<Material>(
            "game://bare", [&]() { return Material::create(*fixture.device, std::move(desc)); });
        REQUIRE(bare.isReady());
        const MaterialInstance empty = *MaterialInstance::create(*fixture.device, bare);
        CHECK(empty.group().isNull());
        packet.sprites = {{.texture = texture, .material = &empty}};
        static_cast<void>(frame());
        CHECK(sprites->stats().drawCalls == 0);
    }
    sprites->forgetMaterial(glow.get());
    renderer.reset();
    fixture.device->destroy(texture);
    fixture.device->destroy(output);
}
