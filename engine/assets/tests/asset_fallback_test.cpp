// Fallback assets: failed loads resolve to the fallback of their type (by path suffix), with one error
// logged, and fallbacks are created once and owned by the registry.

#include <teserra/assets/asset_registry.hpp>
#include <teserra/core/log.hpp>
#include <teserra/core/result.hpp>
#include <teserra/test/assert.hpp>
#include <teserra/test/log_capture.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <string>
#include <string_view>

using namespace teserra;
using namespace teserra::assets;
using Catch::Matchers::ContainsSubstring;

namespace {

// Counts its live instances, to see that fallbacks are created once and destroyed with the registry.
struct Picture {
    explicit Picture(std::string pictureName)
        : name(std::move(pictureName)) {
        ++alive;
    }
    Picture(const Picture& other)
        : name(other.name) {
        ++alive;
    }
    Picture(Picture&& other) noexcept
        : name(std::move(other.name)) {
        ++alive;
    }
    Picture& operator=(const Picture&) = default;
    Picture& operator=(Picture&&) noexcept = default;
    ~Picture() { --alive; }

    std::string name;
    static inline int alive = 0;
};

Result<Picture> missing() {
    return fail(ErrorCode::NotFound, Module::Game, "File not found");
}

Result<Picture> loaded(std::string_view name) {
    return Picture(std::string(name));
}

usize errorCount(const test::LogCapture& logs) {
    return static_cast<usize>(std::ranges::count_if(
        logs.records(), [](const test::CapturedLog& record) { return record.level >= LogLevel::Error; }));
}

} // namespace

TEST_CASE("Fallback assets: a failed load resolves to the fallback and logs one error",
          "[assets][fallback]") {
    AssetRegistry registry;
    registry.setFallback(Picture("checkerboard"));
    const test::LogCapture logs;

    const Asset<Picture> hero = registry.load<Picture>("assets://hero.png", missing);
    const Asset<Picture> again = registry.load<Picture>("assets://hero.png", missing);

    REQUIRE(hero.isReady());
    CHECK(hero.isFallback());
    CHECK_FALSE(hero.isLoaded());
    CHECK(hero.get().name == "checkerboard");
    CHECK(again == hero);
    CHECK(errorCount(logs) == 1);
    CHECK_THAT(logs.records().front().message, ContainsSubstring("assets://hero.png"));
}

TEST_CASE("Fallback assets: a loaded asset is not a fallback", "[assets][fallback]") {
    AssetRegistry registry;
    registry.setFallback(Picture("checkerboard"));

    const Asset<Picture> hero = registry.load<Picture>("assets://hero.png", [] { return loaded("hero"); });

    CHECK(hero.isReady());
    CHECK(hero.isLoaded());
    CHECK_FALSE(hero.isFallback());
    CHECK(hero.get().name == "hero");
}

TEST_CASE("Fallback assets: without a fallback, a failed load is not ready", "[assets][fallback]") {
    AssetRegistry registry;
    const test::LogCapture logs;

    const Asset<Picture> hero = registry.load<Picture>("assets://hero.png", missing);

    CHECK_FALSE(hero.isReady());
    CHECK_FALSE(hero.isLoaded());
    CHECK_FALSE(hero.isFallback());
    const Asset<Picture> null;
    CHECK_FALSE(null.isFallback());
}

TEST_CASE("Fallback assets: the longest matching path suffix wins, without case", "[assets][fallback]") {
    AssetRegistry registry;
    registry.setFallback(Picture("any"));
    registry.setFallback(Picture("vertex"), ".vert.tshader");
    registry.setFallback(Picture("shader"), ".tshader");
    registry.setFallback(Picture("fragment"), ".FRAG.tshader");

    CHECK(registry.load<Picture>("assets://a.vert.tshader", missing).get().name == "vertex");
    CHECK(registry.load<Picture>("assets://b.Frag.TSHADER", missing).get().name == "fragment");
    CHECK(registry.load<Picture>("assets://c.comp.tshader", missing).get().name == "shader");
    CHECK(registry.load<Picture>("assets://d.png", missing).get().name == "any");
}

TEST_CASE("Fallback assets: only the paths with the suffix use a suffixed fallback", "[assets][fallback]") {
    AssetRegistry registry;
    registry.setFallback(Picture("vertex"), ".vert.tshader");

    CHECK(registry.load<Picture>("assets://a.vert.tshader", missing).isFallback());
    CHECK_FALSE(registry.load<Picture>("assets://a.frag.tshader", missing).isReady());
}

TEST_CASE("Fallback assets: failed slots that exist already are bound to a new fallback",
          "[assets][fallback]") {
    AssetRegistry registry;
    const Asset<Picture> hero = registry.load<Picture>("assets://hero.png", missing);
    REQUIRE_FALSE(hero.isReady());

    registry.setFallback(Picture("checkerboard"));
    CHECK(hero.isFallback());
    CHECK(hero.get().name == "checkerboard");

    registry.setFallback(Picture("png"), ".png");
    CHECK(hero.get().name == "png");
}

TEST_CASE("Fallback assets: fallbacks are per type", "[assets][fallback]") {
    AssetRegistry registry;
    registry.setFallback(std::string("text"));

    const Asset<Picture> picture = registry.load<Picture>("assets://hero.png", missing);
    const Asset<std::string> text = registry.load<std::string>(
        "assets://hero.txt", []() -> Result<std::string> { return Unexpected(missing().error()); });

    CHECK_FALSE(picture.isReady());
    REQUIRE(text.isFallback());
    CHECK(text.get() == "text");
}

TEST_CASE("Fallback assets: created once, shared by failed slots, destroyed with the registry",
          "[assets][fallback]") {
    const int before = Picture::alive;
    {
        AssetRegistry registry;
        registry.setFallback(Picture("checkerboard"));
        CHECK(Picture::alive == before + 1);
        {
            const Asset<Picture> first = registry.load<Picture>("assets://a.png", missing);
            const Asset<Picture> second = registry.load<Picture>("assets://b.png", missing);
            CHECK(&first.get() == &second.get());
            CHECK(Picture::alive == before + 1);
        }
        // Unloading the failed slots keeps the fallback.
        registry.endFrame();
        CHECK(registry.size() == 0);
        CHECK(Picture::alive == before + 1);
    }
    CHECK(Picture::alive == before);
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("Fallback assets: a second fallback for one type and suffix asserts", "[assets][fallback]") {
    AssetRegistry registry;
    registry.setFallback(Picture("a"));
    registry.setFallback(Picture("b"), ".png");

    REQUIRE_ASSERTS(registry.setFallback(Picture("c")));
    REQUIRE_ASSERTS(registry.setFallback(Picture("d"), ".PNG"));
}
#endif
