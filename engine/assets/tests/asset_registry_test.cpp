// AssetRegistry and Asset<T>: deduplication, reference counts, unloading at the end of the frame, failed
// loads and the assertions for collisions, types and cycles.

#include <teserra/assets/asset_registry.hpp>

#include <teserra/core/result.hpp>
#include <teserra/test/allocation_counter.hpp>
#include <teserra/test/assert.hpp>
#include <teserra/test/log_capture.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string_view>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::assets;
using Catch::Matchers::ContainsSubstring;

namespace {

// Counts its live instances, to see when the registry unloads it.
struct Payload {
    explicit Payload(int number)
        : value(number) {
        ++alive;
    }
    Payload(const Payload& other)
        : value(other.value) {
        ++alive;
    }
    Payload(Payload&& other) noexcept
        : value(other.value) {
        ++alive;
    }
    Payload& operator=(const Payload&) = default;
    Payload& operator=(Payload&&) noexcept = default;
    ~Payload() { --alive; }

    int value;
    static inline int alive = 0;
};

// An asset that holds another one, like a material holds its textures.
struct Holder {
    Asset<Payload> dependency;
};

// Returns `value` and counts its calls.
struct Create {
    int* calls;
    int value;

    Result<Payload> operator()() const {
        ++*calls;
        return Payload(value);
    }
};

Result<Payload> failToLoad() {
    return fail(ErrorCode::NotFound, Module::Assets, "no such file");
}

} // namespace

TEST_CASE("AssetRegistry: loading a path twice returns the same slot and creates once",
          "[assets][registry]") {
    AssetRegistry assets;
    int calls = 0;

    const Asset<Payload> first = assets.load<Payload>("assets://hero.png", Create{&calls, 7});
    const Asset<Payload> second = assets.load<Payload>("ASSETS://hero.png", Create{&calls, 8});
    const Asset<Payload> other = assets.load<Payload>("assets://enemy.png", Create{&calls, 9});

    CHECK(calls == 2);
    CHECK(first == second);
    CHECK(first.handle() == second.handle());
    CHECK(first != other);
    CHECK(first.isReady());
    CHECK(second.get().value == 7);
    CHECK(other.get().value == 9);
    CHECK(first.id() == AssetId::fromPath("assets://hero.png"));
    CHECK(assets.path(first.id()) == "assets://hero.png");
    CHECK(assets.path(AssetId::fromPath("assets://missing.png")).empty());
    CHECK(assets.size() == 2);
}

TEST_CASE("AssetRegistry: an asset without references is unloaded at the end of the frame",
          "[assets][registry]") {
    AssetRegistry assets;
    int calls = 0;
    {
        Asset<Payload> hero = assets.load<Payload>("assets://hero.png", Create{&calls, 1});
        Asset<Payload> copy = hero;
        hero = {};
        CHECK(copy.isReady());
        assets.endFrame();
        CHECK(Payload::alive == 1); // `copy` still refers to it
    }
    // The last reference is gone, but the asset stays until the end of the frame.
    CHECK(Payload::alive == 1);
    CHECK(assets.size() == 1);

    assets.endFrame();
    CHECK(Payload::alive == 0);
    CHECK(assets.size() == 0);
    CHECK(assets.path(AssetId::fromPath("assets://hero.png")).empty());

    // Loading it again creates it again.
    const Asset<Payload> again = assets.load<Payload>("assets://hero.png", Create{&calls, 2});
    CHECK(calls == 2);
    CHECK(again.get().value == 2);
}

TEST_CASE("AssetRegistry: an asset dropped and loaded again in one frame is kept", "[assets][registry]") {
    AssetRegistry assets;
    int calls = 0;
    AssetHandle slot;
    {
        const Asset<Payload> hero = assets.load<Payload>("assets://hero.png", Create{&calls, 1});
        slot = hero.handle();
    }
    const Asset<Payload> again = assets.load<Payload>("assets://hero.png", Create{&calls, 2});
    assets.endFrame();

    CHECK(calls == 1);
    CHECK(again.handle() == slot);
    CHECK(again.get().value == 1);
    CHECK(Payload::alive == 1);
}

TEST_CASE("Asset<T>: copies and moves keep the count right", "[assets][registry]") {
    AssetRegistry assets;
    int calls = 0;
    {
        Asset<Payload> original = assets.load<Payload>("assets://hero.png", Create{&calls, 1});
        Asset<Payload> moved = std::move(original);
        // What moving leaves behind: a null asset.
        CHECK(original.isNull());        // NOLINT(bugprone-use-after-move)
        CHECK_FALSE(original.isReady()); // NOLINT(bugprone-use-after-move)
        CHECK(moved.isReady());

        Asset<Payload> assigned;
        assigned = moved; // copy
        const Asset<Payload>& self = assigned;
        assigned = self; // self-assignment keeps the reference
        Asset<Payload> moveAssigned;
        moveAssigned = std::move(moved);

        std::vector<Asset<Payload>> copies(5, assigned);
        copies.clear();
        assets.endFrame();
        CHECK(Payload::alive == 1); // `assigned` and `moveAssigned` remain
    }
    assets.endFrame();
    CHECK(Payload::alive == 0);
}

TEST_CASE("Asset<T>: a null asset is not ready and has no ID", "[assets][registry]") {
    const Asset<Payload> none;
    CHECK(none.isNull());
    CHECK_FALSE(none.isReady());
    CHECK(none.id().isNull());
    CHECK(none.handle().isNull());
    CHECK(none == Asset<Payload>{});
}

TEST_CASE("AssetRegistry: a failed load is logged once and deduplicated", "[assets][registry]") {
    AssetRegistry assets;
    test::LogCapture log;

    const Asset<Payload> first = assets.load<Payload>("assets://missing.png", failToLoad);
    int calls = 0;
    const Asset<Payload> second = assets.load<Payload>("assets://missing.png", Create{&calls, 1});

    CHECK(calls == 0);
    CHECK(first == second);
    CHECK_FALSE(first.isNull());
    CHECK_FALSE(first.isReady());
    CHECK(first.id() == AssetId::fromPath("assets://missing.png"));
    REQUIRE(log.records().size() == 1);
    CHECK(log.records()[0].level == LogLevel::Error);
    CHECK_THAT(log.records()[0].message,
               ContainsSubstring("assets://missing.png") && ContainsSubstring("no such file"));
}

TEST_CASE("AssetRegistry: create may load dependencies", "[assets][registry]") {
    AssetRegistry assets;
    int calls = 0;

    {
        const Asset<Holder> holder = assets.load<Holder>("assets://material.json", [&] {
            return Result<Holder>(Holder{assets.load<Payload>("assets://texture.png", Create{&calls, 3})});
        });
        CHECK(holder.get().dependency.get().value == 3);
        CHECK(assets.size() == 2);
    }
    // Unloading the holder releases its dependency, which is unloaded in the same endFrame().
    assets.endFrame();
    CHECK(assets.size() == 0);
    CHECK(Payload::alive == 0);
}

TEST_CASE("AssetRegistry: a steady-state endFrame allocates nothing", "[assets][registry]") {
    AssetRegistry assets;
    int calls = 0;
    const Asset<Payload> kept = assets.load<Payload>("assets://kept.png", Create{&calls, 1});

    // Warm-up: grows the unload lists once.
    for (int frame = 0; frame < 2; ++frame) {
        Asset<Payload> copy = kept;
        copy = {};
        assets.endFrame();
    }

    const test::AllocationCounter allocations;
    for (int frame = 0; frame < 10; ++frame) {
        Asset<Payload> copy = kept;
        copy = {};
        assets.endFrame();
    }
    CHECK(allocations.count() == 0);
    CHECK(kept.isReady());
}

#if TESERRA_ENABLE_ASSERTS

namespace {

struct Other {
    int value = 0;
};

u64 sameHashForEveryPath(std::string_view) {
    return 42;
}

} // namespace

TEST_CASE("Asset<T>: reading a null or failed asset asserts", "[assets][registry][validation]") {
    const Asset<Payload> none;
    CHECK_THAT(test::requireAssert([&] { (void)none.get(); }).message(), ContainsSubstring("null asset"));

    AssetRegistry assets;
    const test::LogCapture log;
    const Asset<Payload> missing = assets.load<Payload>("assets://missing.png", failToLoad);
    CHECK_THAT(test::requireAssert([&] { (void)missing.get(); }).message(), ContainsSubstring("not ready"));
}

TEST_CASE("AssetRegistry: two paths with the same hash assert", "[assets][registry][validation]") {
    AssetRegistry assets({.hashPath = sameHashForEveryPath});
    int calls = 0;

    const Asset<Payload> hero = assets.load<Payload>("assets://hero.png", Create{&calls, 1});
    const auto failure =
        test::requireAssert([&] { (void)assets.load<Payload>("assets://enemy.png", Create{&calls, 2}); });

    CHECK_THAT(failure.message(),
               ContainsSubstring("assets://hero.png") && ContainsSubstring("assets://enemy.png"));
    CHECK(calls == 1);
    CHECK(hero.get().value == 1);
}

TEST_CASE("AssetRegistry: one path loaded or read as two types asserts", "[assets][registry][validation]") {
    AssetRegistry assets;
    int calls = 0;
    const Asset<Payload> hero = assets.load<Payload>("assets://hero.png", Create{&calls, 1});

    const auto failure = test::requireAssert(
        [&] { (void)assets.load<Other>("assets://hero.png", [] { return Result<Other>(Other{}); }); });
    CHECK_THAT(failure.message(), ContainsSubstring("another type"));
    CHECK(assets.size() == 1);
}

TEST_CASE("AssetRegistry: an asset that loads itself asserts", "[assets][registry][validation]") {
    AssetRegistry assets;
    int calls = 0;

    const auto failure = test::requireAssert([&] {
        (void)assets.load<Holder>("assets://loop.json", [&] {
            return Result<Holder>(Holder{assets.load<Payload>("assets://loop.json", Create{&calls, 4})});
        });
    });
    CHECK_THAT(failure.message(), ContainsSubstring("cycle"));
    assets.endFrame();
    CHECK(assets.size() == 0);
}

#endif // TESERRA_ENABLE_ASSERTS
