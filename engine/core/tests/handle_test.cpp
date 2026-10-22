#include <teserra/core/handle.hpp>

#include <teserra/core/handle_pool.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_get_random_seed.hpp>
#include <catch2/catch_test_macros.hpp>

#include <format>
#include <memory>
#include <random>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace teserra;

namespace {

using BufferHandle = Handle<struct BufferTag>;
using TextureHandle = Handle<struct TextureTag>;

// Counts live instances, to check that the pool constructs and destroys objects.
struct Tracked {
    explicit Tracked(int& liveCount, int id)
        : live(&liveCount), value(id) {
        ++*live;
    }
    Tracked(Tracked&& other) noexcept
        : live(std::exchange(other.live, nullptr)), value(other.value) {}
    Tracked& operator=(Tracked&&) = delete;
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
    ~Tracked() {
        if (live != nullptr) {
            --*live;
        }
    }

    int* live;
    int value;
};

} // namespace

// Vulkan readiness: handles are small trivially copyable values with no public data members.
static_assert(sizeof(BufferHandle) == 8);
static_assert(std::is_trivially_copyable_v<BufferHandle>);
static_assert(std::is_standard_layout_v<BufferHandle>);
// Distinct tags are distinct types without conversions.
static_assert(!std::is_convertible_v<BufferHandle, TextureHandle>);
static_assert(!std::is_convertible_v<TextureHandle, BufferHandle>);
static_assert(!std::is_constructible_v<TextureHandle, BufferHandle>);
static_assert(!std::is_assignable_v<TextureHandle&, BufferHandle>);

TEST_CASE("Handle: default is null; accessors, comparison, hashing and formatting", "[core][handle]") {
    constexpr BufferHandle kNull;
    constexpr BufferHandle kFirst(0, 1);
    constexpr BufferHandle kReused(0, 2);

    STATIC_CHECK(kNull.isNull());
    STATIC_CHECK_FALSE(kFirst.isNull());
    STATIC_CHECK(kFirst.index() == 0);
    STATIC_CHECK(kFirst.generation() == 1);
    STATIC_CHECK(kFirst != kReused);
    STATIC_CHECK(kFirst < kReused);
    STATIC_CHECK(BufferHandle(7, 0).isNull());

    const std::unordered_set<BufferHandle> set{kFirst, kReused, kFirst};
    CHECK(set.size() == 2);
    CHECK(std::format("{}", BufferHandle(3, 2)) == "3:2");
    CHECK(std::format("{}", kNull) == "null");
}

TEST_CASE("HandlePool: create, get, destroy", "[core][handle]") {
    HandlePool<std::string, BufferTag> pool;
    const BufferHandle hero = pool.create("hero");
    const BufferHandle tiles = pool.create(3, 'x');

    CHECK(pool.size() == 2);
    CHECK(pool.get(hero) == "hero");
    CHECK(pool.get(tiles) == "xxx");
    pool.get(hero) += "!";
    CHECK(std::as_const(pool).get(hero) == "hero!");

    pool.destroy(hero);
    CHECK(pool.size() == 1);
    CHECK_FALSE(pool.contains(hero));
    CHECK(pool.contains(tiles));
    CHECK(pool.tryGet(hero) == nullptr);
    CHECK(std::as_const(pool).tryGet(tiles) != nullptr);
}

TEST_CASE("HandlePool: null, stale and out-of-range handles are rejected", "[core][handle]") {
    HandlePool<int, BufferTag> pool;
    const BufferHandle handle = pool.create(1);
    pool.destroy(handle);
    const BufferHandle reuse = pool.create(2);

    CHECK(reuse.index() == handle.index());
    CHECK(reuse.generation() == handle.generation() + 1);
    CHECK(pool.tryGet(handle) == nullptr);
    CHECK(pool.tryGet(BufferHandle()) == nullptr);
    CHECK(pool.tryGet(BufferHandle(99, 1)) == nullptr);
    CHECK(pool.get(reuse) == 2);
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("HandlePool: stale handles assert in get and destroy", "[core][handle]") {
    HandlePool<int, BufferTag> pool;
    const BufferHandle handle = pool.create(1);
    pool.destroy(handle);

    const auto failure = test::requireAssert([&] { static_cast<void>(pool.get(handle)); });
    CHECK(failure.message() == std::format("Stale or null handle {}", handle));
    REQUIRE_ASSERTS(pool.destroy(handle));
    REQUIRE_ASSERTS(static_cast<void>(pool.get(BufferHandle())));
    CHECK(pool.empty());
}

TEST_CASE("HandlePool: destroying during forEach asserts", "[core][handle]") {
    HandlePool<int, BufferTag> pool;
    const BufferHandle handle = pool.create(1);

    REQUIRE_ASSERTS(pool.forEach([&](BufferHandle, int&) { pool.destroy(handle); }));
    // The iteration counter was restored although the assertion threw.
    pool.destroy(handle);
    CHECK(pool.empty());
}
#endif

TEST_CASE("HandlePool: freed slots are reused in FIFO order", "[core][handle]") {
    HandlePool<int, BufferTag> pool;
    const BufferHandle a = pool.create(0);
    const BufferHandle b = pool.create(1);
    const BufferHandle c = pool.create(2);

    pool.destroy(b);
    pool.destroy(a);
    pool.destroy(c);

    CHECK(pool.create(10).index() == b.index());
    CHECK(pool.create(11).index() == a.index());
    CHECK(pool.create(12).index() == c.index());
    CHECK(pool.create(13).index() == 3); // free list exhausted: a new slot
}

TEST_CASE("HandlePool: a slot at the maximum generation is retired, never reused", "[core][handle]") {
    HandlePool<int, BufferTag, 3> pool;

    BufferHandle handle = pool.create(0);
    for (u32 generation = 1; generation < 3; ++generation) {
        CHECK(handle == BufferHandle(0, generation));
        pool.destroy(handle);
        handle = pool.create(0);
    }
    CHECK(handle == BufferHandle(0, 3));
    pool.destroy(handle);

    CHECK(pool.retiredSlots() == 1);
    const BufferHandle next = pool.create(1);
    CHECK(next.index() == 1);
    CHECK(next.generation() == 1);
    CHECK(pool.tryGet(handle) == nullptr);
}

TEST_CASE("HandlePool: objects are constructed in place and destroyed", "[core][handle]") {
    int live = 0;
    {
        HandlePool<Tracked, BufferTag> pool;
        const BufferHandle first = pool.create(live, 1);
        static_cast<void>(pool.create(live, 2));
        CHECK(live == 2);

        pool.destroy(first);
        CHECK(live == 1);

        static_cast<void>(pool.create(live, 3));
        pool.clear();
        CHECK(live == 0);
        CHECK(pool.empty());

        static_cast<void>(pool.create(live, 4));
    }
    CHECK(live == 0); // the pool's destructor destroys what is left
}

TEST_CASE("HandlePool: move-only objects", "[core][handle]") {
    HandlePool<std::unique_ptr<int>, BufferTag> pool;
    const BufferHandle handle = pool.create(std::make_unique<int>(5));

    CHECK(*pool.get(handle) == 5);
}

TEST_CASE("HandlePool: forEach visits live objects in slot order", "[core][handle]") {
    HandlePool<int, BufferTag> pool;
    const BufferHandle a = pool.create(10);
    const BufferHandle b = pool.create(20);
    const BufferHandle c = pool.create(30);
    pool.destroy(b);

    std::vector<std::pair<BufferHandle, int>> visited;
    pool.forEach([&](BufferHandle handle, int& value) {
        visited.emplace_back(handle, value);
        value += 1;
    });
    CHECK(visited == std::vector<std::pair<BufferHandle, int>>{{a, 10}, {c, 30}});

    int sum = 0;
    std::as_const(pool).forEach([&](BufferHandle, const int& value) { sum += value; });
    CHECK(sum == 11 + 31);
}

// Applies random operations to the pool and to a std::unordered_map model, and compares them after
// every step. The seed is Catch2's (--rng-seed), printed when the test fails.
TEST_CASE("HandlePool: randomized operations match an unordered_map model", "[core][handle]") {
    const u32 seed = Catch::getSeed();
    INFO("seed " << seed);
    std::mt19937 random(seed);

    // Small MaxGeneration so that slots are retired during the run.
    HandlePool<int, BufferTag, 8> pool;
    std::unordered_map<BufferHandle, int> model;
    std::vector<BufferHandle> issued;           // every handle ever returned, live or stale
    std::unordered_set<BufferHandle> issuedSet; // the same, for uniqueness checks
    int nextValue = 0;

    // One REQUIRE per check and step (not per element), so the test stays fast in Debug builds.
    const auto poolMatchesModel = [&] {
        if (pool.size() != model.size()) {
            return false;
        }
        for (const auto& [handle, value] : model) {
            const int* object = pool.tryGet(handle);
            if (object == nullptr || *object != value) {
                return false;
            }
        }
        usize visited = 0;
        bool visitedMatch = true;
        pool.forEach([&](BufferHandle handle, const int& value) {
            ++visited;
            const auto expected = model.find(handle);
            visitedMatch = visitedMatch && expected != model.end() && expected->second == value;
        });
        return visitedMatch && visited == model.size();
    };

    for (int step = 0; step < 2000; ++step) {
        INFO("step " << step);
        const u32 operation = random() % 10;
        if (operation < 5 || model.empty()) {
            const BufferHandle handle = pool.create(nextValue);
            // A new handle never equals any handle handed out before, live or stale.
            REQUIRE(issuedSet.insert(handle).second);
            model.emplace(handle, nextValue++);
            issued.push_back(handle);
        } else if (operation < 9) {
            auto victim = model.begin();
            std::advance(victim, static_cast<std::ptrdiff_t>(random() % model.size()));
            pool.destroy(victim->first);
            model.erase(victim);
        } else {
            const BufferHandle any = issued[random() % issued.size()];
            REQUIRE(pool.contains(any) == model.contains(any));
        }
        REQUIRE(poolMatchesModel());
    }
    CHECK(pool.retiredSlots() > 0);
}
