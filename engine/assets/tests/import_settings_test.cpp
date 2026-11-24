// Import settings: an asset without a sidecar loads with the defaults; a <file>.asset.json sidecar's
// settings reach the loader; unknown keys are one warning; a malformed sidecar fails the load with its path.

#include <teserra/assets/asset_registry.hpp>
#include <teserra/assets/mount_table.hpp>
#include <teserra/core/log.hpp>
#include <teserra/test/log_capture.hpp>
#include <teserra/test/memory_file_system.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>

using namespace teserra;
using namespace teserra::assets;

namespace {

// An asset that records the import settings its loader saw.
struct SettingsProbe {
    AssetImportSettings settings;
};

class SettingsProbeLoader final : public AssetLoader<SettingsProbe> {
public:
    [[nodiscard]] std::span<const std::string_view> extensions() const override { return kExtensions; }
    [[nodiscard]] Result<SettingsProbe> load(AssetLoadContext& context, std::span<const std::byte>) override {
        return SettingsProbe{.settings = context.settings()};
    }

private:
    static constexpr std::array<std::string_view, 1> kExtensions{"probe"};
};

struct Fixture {
    Fixture() {
        REQUIRE(mounts.mount("assets", "/game/assets"));
        registry.registerLoader<SettingsProbe>(std::make_unique<SettingsProbeLoader>());
        files.addTextFile("/game/assets/a.probe", "data");
    }

    test::MemoryFileSystem files;
    MountTable mounts{files};
    AssetRegistry registry{{.mounts = &mounts}};
};

usize logsMentioning(const test::LogCapture& logs, LogLevel level, std::string_view text) {
    return static_cast<usize>(std::ranges::count_if(logs.records(), [&](const test::CapturedLog& record) {
        return record.level == level && record.message.find(text) != std::string::npos;
    }));
}

} // namespace

TEST_CASE("Import settings: without a sidecar the loader sees the defaults", "[assets][import]") {
    Fixture fixture;
    const Asset<SettingsProbe> probe = fixture.registry.load<SettingsProbe>("assets://a.probe");
    REQUIRE(probe.isLoaded());
    CHECK(probe.get().settings == AssetImportSettings{});
    CHECK(probe.get().settings.srgb);
    CHECK_FALSE(probe.get().settings.mipmaps);
    CHECK(importSettingsPath("assets://a.probe") == "assets://a.probe.asset.json");
}

TEST_CASE("Import settings: a sidecar's settings reach the loader", "[assets][import]") {
    Fixture fixture;
    fixture.files.addTextFile("/game/assets/a.probe.asset.json", R"({"srgb": false, "mipmaps": true})");
    const test::LogCapture logs;
    const Asset<SettingsProbe> probe = fixture.registry.load<SettingsProbe>("assets://a.probe");
    REQUIRE(probe.isLoaded());
    CHECK(probe.get().settings == AssetImportSettings{.srgb = false, .mipmaps = true});
    CHECK(logsMentioning(logs, LogLevel::Warn, "") == 0);
}

TEST_CASE("Import settings: unknown keys are one warning", "[assets][import]") {
    Fixture fixture;
    fixture.files.addTextFile("/game/assets/a.probe.asset.json",
                              R"({"mipmap": true, "srgb": false, "color": 1})");
    const test::LogCapture logs;
    const Asset<SettingsProbe> probe = fixture.registry.load<SettingsProbe>("assets://a.probe");
    REQUIRE(probe.isLoaded());
    CHECK_FALSE(probe.get().settings.srgb); // the known key still applies
    CHECK_FALSE(probe.get().settings.mipmaps);
    CHECK(logsMentioning(
              logs, LogLevel::Warn,
              "Import settings 'assets://a.probe.asset.json': unknown settings ignored: mipmap, color") == 1);
    CHECK(logs.records().size() == 1);
}

TEST_CASE("Import settings: a malformed sidecar fails the load with its path", "[assets][import]") {
    const auto failure = [](std::string_view sidecar, std::string_view reason) {
        Fixture fixture;
        fixture.files.addTextFile("/game/assets/a.probe.asset.json", sidecar);
        const test::LogCapture logs;
        const Asset<SettingsProbe> probe = fixture.registry.load<SettingsProbe>("assets://a.probe");
        CHECK_FALSE(probe.isReady());
        CHECK(logsMentioning(logs, LogLevel::Error, "Import settings 'assets://a.probe.asset.json'") == 1);
        CHECK(logsMentioning(logs, LogLevel::Error, reason) == 1);
    };
    failure(R"({"srgb": false,)", "line 1, column ");
    failure(R"({"srgb": "no"})", "'srgb' must be a boolean, not string");
    failure(R"([true])", "hold array, not an object");
}
