// Loaders: AssetRegistry::load<T>(path) picks the loader registered for T and the path's extension, reads
// the file through the mount points and logs a failure once with the path. A new asset type (MeshStub)
// needs only a loader.

#include <teserra/assets/asset_registry.hpp>
#include <teserra/assets/image.hpp>
#include <teserra/assets/mount_table.hpp>
#include <teserra/core/log.hpp>
#include <teserra/core/result.hpp>
#include <teserra/test/assert.hpp>
#include <teserra/test/log_capture.hpp>
#include <teserra/test/memory_file_system.hpp>
#include <teserra/test/png.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::assets;
using Catch::Matchers::ContainsSubstring;

namespace {

// A stand-in for a mesh: an asset type the registry has never heard of.
struct MeshStub {
    std::string path;
    usize vertexCount = 0;
};

// Reads "<vertex count>" text files. Counts its loads.
class MeshStubLoader final : public AssetLoader<MeshStub> {
public:
    explicit MeshStubLoader(int* loads = nullptr) noexcept
        : m_loads(loads) {}

    [[nodiscard]] std::span<const std::string_view> extensions() const override { return kExtensions; }

    [[nodiscard]] Result<MeshStub> load(AssetLoadContext& context,
                                        std::span<const std::byte> bytes) override {
        if (m_loads != nullptr) {
            ++*m_loads;
        }
        if (bytes.empty()) {
            return fail(ErrorCode::InvalidData, Module::Game, "The mesh is empty");
        }
        return MeshStub{.path = std::string(context.path()), .vertexCount = bytes.size()};
    }

private:
    static constexpr std::array<std::string_view, 2> kExtensions{"mesh", "obj"};
    int* m_loads;
};

// A model: a text file naming its mesh, which it loads as a dependency.
struct ModelStub {
    Asset<MeshStub> mesh;
    std::string readme;
};

class ModelStubLoader final : public AssetLoader<ModelStub> {
public:
    [[nodiscard]] std::span<const std::string_view> extensions() const override { return kExtensions; }

    [[nodiscard]] Result<ModelStub> load(AssetLoadContext& context,
                                         std::span<const std::byte> bytes) override {
        std::string meshPath(bytes.size(), '\0');
        std::ranges::transform(bytes, meshPath.begin(),
                               [](std::byte byte) { return static_cast<char>(byte); });
        ModelStub model{.mesh = context.load<MeshStub>(meshPath), .readme = {}};
        TESERRA_TRY_ASSIGN(const std::vector<std::byte> readme, context.readFile("assets://readme.txt"));
        model.readme.resize(readme.size());
        std::ranges::transform(readme, model.readme.begin(),
                               [](std::byte byte) { return static_cast<char>(byte); });
        return model;
    }

private:
    static constexpr std::array<std::string_view, 1> kExtensions{"model"};
};

// A loader of `T` for `extensions`, which makes a default T.
template <typename T>
class FixedLoader final : public AssetLoader<T> {
public:
    explicit FixedLoader(std::span<const std::string_view> extensions) noexcept
        : m_extensions(extensions) {}

    [[nodiscard]] std::span<const std::string_view> extensions() const override { return m_extensions; }
    [[nodiscard]] Result<T> load(AssetLoadContext&, std::span<const std::byte>) override { return T{}; }

private:
    std::span<const std::string_view> m_extensions;
};

// Files in memory under /game/assets, mounted as assets://, and a registry reading through them.
struct Fixture {
    Fixture() { REQUIRE(mounts.mount("assets", "/game/assets")); }

    test::MemoryFileSystem files;
    MountTable mounts{files};
    AssetRegistry registry{{.mounts = &mounts}};
};

usize errorsMentioning(const test::LogCapture& logs, std::string_view text) {
    return static_cast<usize>(std::ranges::count_if(logs.records(), [&](const test::CapturedLog& record) {
        return record.level == LogLevel::Error && record.message.find(text) != std::string::npos;
    }));
}

} // namespace

TEST_CASE("Asset loaders: a new asset type needs only a loader", "[assets][loader]") {
    Fixture fixture;
    fixture.files.addTextFile("/game/assets/meshes/rock.mesh", "12345");
    int loads = 0;
    fixture.registry.registerLoader<MeshStub>(std::make_unique<MeshStubLoader>(&loads));

    const Asset<MeshStub> rock = fixture.registry.load<MeshStub>("assets://meshes/rock.mesh");

    REQUIRE(rock.isReady());
    CHECK(rock.get().vertexCount == 5);
    CHECK(rock.get().path == "assets://meshes/rock.mesh");
    CHECK(rock.id() == AssetId::fromPath("assets://meshes/rock.mesh"));
    // The loader also looked for the import-settings sidecar next to the file.
    CHECK(fixture.files.reads() == std::vector<std::string>{"/game/assets/meshes/rock.mesh",
                                                            "/game/assets/meshes/rock.mesh.asset.json"});

    SECTION("loading the path again reads nothing") {
        const Asset<MeshStub> again = fixture.registry.load<MeshStub>("ASSETS://meshes\\rock.mesh");
        CHECK(again == rock);
        CHECK(loads == 1);
        CHECK(fixture.files.reads().size() == 2);
    }
}

TEST_CASE("Asset loaders: the loader is chosen by type and extension, without case", "[assets][loader]") {
    Fixture fixture;
    fixture.files.addTextFile("/game/assets/rock.OBJ", "123");
    fixture.files.addTextFile("/game/assets/hero.png", "not a PNG");
    fixture.registry.registerLoader<MeshStub>(std::make_unique<MeshStubLoader>());
    fixture.registry.registerLoader<ImageData>(std::make_unique<ImageLoader>());

    // .OBJ is a mesh extension; the extension of the last component counts, not a dot in a directory.
    const Asset<MeshStub> rock = fixture.registry.load<MeshStub>("assets://rock.OBJ");
    CHECK(rock.isReady());

    const test::LogCapture logs;
    SECTION("a type without a loader for the extension is Unsupported") {
        const Asset<MeshStub> image = fixture.registry.load<MeshStub>("assets://hero.png");
        CHECK_FALSE(image.isReady());
        CHECK(errorsMentioning(logs, "No loader of this asset type reads '.png' files") == 1);
    }
    SECTION("a path without an extension has no loader") {
        fixture.files.addTextFile("/game/assets/meshes.d/rock", "1");
        const Asset<MeshStub> rockWithoutExtension =
            fixture.registry.load<MeshStub>("assets://meshes.d/rock");
        CHECK_FALSE(rockWithoutExtension.isReady());
        CHECK(errorsMentioning(logs, "has no extension") == 1);
    }
    // Neither failure read a file: the reads are the rock's and its (missing) import-settings sidecar's.
    CHECK(fixture.files.reads().size() == 2);
}

TEST_CASE("Asset loaders: a failed load is logged once with the path and deduplicated", "[assets][loader]") {
    Fixture fixture;
    fixture.files.addTextFile("/game/assets/empty.mesh", "");
    int loads = 0;
    fixture.registry.registerLoader<MeshStub>(std::make_unique<MeshStubLoader>(&loads));
    const test::LogCapture logs;

    SECTION("a missing file") {
        const Asset<MeshStub> first = fixture.registry.load<MeshStub>("assets://missing.mesh");
        const Asset<MeshStub> second = fixture.registry.load<MeshStub>("assets://missing.mesh");
        CHECK_FALSE(first.isReady());
        CHECK(first == second);
        CHECK(loads == 0);
        CHECK(errorsMentioning(logs, "Cannot load 'assets://missing.mesh'") == 1);
        CHECK(errorsMentioning(logs, "/game/assets/missing.mesh") == 1);
        CHECK(fixture.files.reads().size() == 1);
    }
    SECTION("the loader's error") {
        const Asset<MeshStub> first = fixture.registry.load<MeshStub>("assets://empty.mesh");
        const Asset<MeshStub> second = fixture.registry.load<MeshStub>("assets://empty.mesh");
        CHECK_FALSE(second.isReady());
        CHECK(loads == 1);
        CHECK(errorsMentioning(logs, "Cannot load 'assets://empty.mesh': The mesh is empty") == 1);
    }
    SECTION("a path that leaves its mount") {
        const Asset<MeshStub> escaped = fixture.registry.load<MeshStub>("assets://../save.mesh");
        CHECK_FALSE(escaped.isReady());
        CHECK(errorsMentioning(logs, "'..'") == 1);
        CHECK(fixture.files.reads().empty());
    }
    CHECK(logs.records().size() == 1);
}

TEST_CASE("Asset loaders: a registry without a mount table cannot read files", "[assets][loader]") {
    AssetRegistry registry;
    registry.registerLoader<MeshStub>(std::make_unique<MeshStubLoader>());
    const test::LogCapture logs;

    const Asset<MeshStub> rock = registry.load<MeshStub>("assets://rock.mesh");

    CHECK_FALSE(rock.isReady());
    CHECK(errorsMentioning(logs, "no mount table") == 1);
}

TEST_CASE("Asset loaders: loaders load dependencies and read other files through the context",
          "[assets][loader]") {
    Fixture fixture;
    fixture.files.addTextFile("/game/assets/rock.model", "assets://meshes/rock.mesh");
    fixture.files.addTextFile("/game/assets/meshes/rock.mesh", "1234");
    fixture.files.addTextFile("/game/assets/readme.txt", "hello");
    fixture.registry.registerLoader<MeshStub>(std::make_unique<MeshStubLoader>());
    fixture.registry.registerLoader<ModelStub>(std::make_unique<ModelStubLoader>());

    Asset<ModelStub> model = fixture.registry.load<ModelStub>("assets://rock.model");

    REQUIRE(model.isReady());
    REQUIRE(model.get().mesh.isReady());
    CHECK(model.get().mesh.get().vertexCount == 4);
    CHECK(model.get().readme == "hello");
    CHECK(fixture.registry.size() == 2);

    // The model holds its mesh: unloading the model unloads the mesh in the same endFrame().
    model = {};
    fixture.registry.endFrame();
    CHECK(fixture.registry.size() == 0);
}

TEST_CASE("Asset loaders: ImageLoader decodes PNG files into ImageData", "[assets][loader]") {
    Fixture fixture;
    constexpr std::array<u8, 8> kPixels{255, 0, 0, 255, 0, 0, 255, 128};
    fixture.files.addFile("/game/assets/sprites/hero.png",
                          test::writePng(2, 1, test::PngColor::Rgba, kPixels));
    fixture.registry.registerLoader<ImageData>(std::make_unique<ImageLoader>());

    const Asset<ImageData> hero = fixture.registry.load<ImageData>("assets://sprites/hero.png");

    REQUIRE(hero.isReady());
    CHECK(hero.get().width == 2);
    CHECK(hero.get().height == 1);
    CHECK(hero.get().pixels.size() == 8);
    CHECK(hero.get().pixels[6] == std::byte{255});

    SECTION("with the limits of its config") {
        AssetRegistry strict{{.mounts = &fixture.mounts}};
        strict.registerLoader<ImageData>(std::make_unique<ImageLoader>(ImageDecodeConfig{.maxDimension = 1}));
        const test::LogCapture logs;
        const Asset<ImageData> tooWide = strict.load<ImageData>("assets://sprites/hero.png");
        CHECK_FALSE(tooWide.isReady());
        CHECK(errorsMentioning(logs, "Cannot load 'assets://sprites/hero.png'") == 1);
    }
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("Asset loaders: registering two loaders of one type for one extension asserts",
          "[assets][loader]") {
    AssetRegistry registry;
    registry.registerLoader<MeshStub>(std::make_unique<MeshStubLoader>());

    static constexpr std::array<std::string_view, 1> kObj{"obj"};
    REQUIRE_ASSERTS(registry.registerLoader<MeshStub>(std::make_unique<FixedLoader<MeshStub>>(kObj)));
    // Another type may read the same extension.
    registry.registerLoader<ModelStub>(std::make_unique<FixedLoader<ModelStub>>(kObj));
}

TEST_CASE("Asset loaders: extensions must be lowercase and without the dot", "[assets][loader]") {
    AssetRegistry registry;
    static constexpr std::array<std::string_view, 1> kDotted{".mesh"};
    static constexpr std::array<std::string_view, 1> kUppercase{"Mesh"};
    static constexpr std::array<std::string_view, 1> kEmpty{""};

    REQUIRE_ASSERTS(registry.registerLoader<MeshStub>(std::make_unique<FixedLoader<MeshStub>>(kDotted)));
    REQUIRE_ASSERTS(registry.registerLoader<MeshStub>(std::make_unique<FixedLoader<MeshStub>>(kUppercase)));
    REQUIRE_ASSERTS(registry.registerLoader<MeshStub>(std::make_unique<FixedLoader<MeshStub>>(kEmpty)));
    REQUIRE_ASSERTS(registry.registerLoader<MeshStub>(nullptr));
}
#endif
