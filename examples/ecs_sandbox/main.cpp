// ECS sandbox: the iteration 9 demo (#67). A console program that uses the ECS the way a game would:
// 100 000 entities with positions and velocities, some with health, some frozen, moved through views, and
// timings of each step printed, so the cost of the storage model (ADR-0019) is visible.
//
//   teserra_ecs_sandbox                      100 000 entities
//   teserra_ecs_sandbox --entities 1000000   another count
//
// Build with the Release preset (or the CI release job) for meaningful timings; Debug numbers are many
// times higher.

#include <teserra/core/log.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/world.hpp>

#include <charconv>
#include <chrono>
#include <string_view>
#include <system_error>
#include <vector>

using namespace teserra;

namespace {

struct Position {
    float x = 0.0F;
    float y = 0.0F;
};
struct Velocity {
    float dx = 1.0F;
    float dy = 0.5F;
};
struct Health {
    float points = 100.0F;
};
struct Frozen {}; // a tag: frozen entities do not move

constexpr u32 kDefaultEntities = 100'000;
constexpr float kDeltaSeconds = 1.0F / 60.0F;

u32 entityCountArgument(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) ==
            "--entities") { // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            const std::string_view value =
                argv[i + 1]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            u32 count = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), count);
            if (error == std::errc{} && end == value.data() + value.size() && count > 0) {
                return count;
            }
            TESERRA_LOG_WARN(LogCategory::App, "--entities needs a positive number, not '{}'", value);
        }
    }
    return kDefaultEntities;
}

// Runs `step` and logs how long it took.
template <typename Step>
void timed(std::string_view name, Step&& step) {
    const auto start = std::chrono::steady_clock::now();
    step();
    const auto micros =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    TESERRA_LOG_INFO(LogCategory::App, "{:<44} {:>10.1f} us", name, micros);
}

} // namespace

int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
    const u32 count = entityCountArgument(argc, argv);
    TESERRA_LOG_INFO(LogCategory::App, "ECS sandbox: {} entities ({} build)", count,
                     TESERRA_DEBUG_BUILD != 0 ? "Debug" : "optimized");

    ecs::World world;
    std::vector<ecs::Entity> entities;
    entities.reserve(count);

    timed("create, with Position and Velocity", [&] {
        for (u32 i = 0; i < count; ++i) {
            const ecs::Entity entity = world.create();
            world.add<Position>(entity, static_cast<float>(i), 0.0F);
            world.add<Velocity>(entity);
            entities.push_back(entity);
        }
    });
    timed("add Health to every 2nd, Frozen to every 10th", [&] {
        for (u32 i = 0; i < count; ++i) {
            if (i % 2 == 0) {
                world.add<Health>(entities[i]);
            }
            if (i % 10 == 0) {
                world.add<Frozen>(entities[i]);
            }
        }
    });

    usize moved = 0;
    timed("view<Position, const Velocity>", [&] {
        for (auto [entity, position, velocity] : world.view<Position, const Velocity>()) {
            position.x += velocity.dx * kDeltaSeconds;
            position.y += velocity.dy * kDeltaSeconds;
        }
    });
    timed("view<Position, const Velocity>(exclude<Frozen>)", [&] {
        for (auto [entity, position, velocity] : world.view<Position, const Velocity>(ecs::exclude<Frozen>)) {
            position.x += velocity.dx * kDeltaSeconds;
            ++moved;
        }
    });
    float totalHealth = 0.0F;
    timed("view<const Position, Health, const Velocity>", [&] {
        for (auto [entity, position, health, velocity] :
             world.view<const Position, Health, const Velocity>()) {
            health.points -= velocity.dx * kDeltaSeconds;
            totalHealth += health.points;
        }
    });
    timed("each<Position>", [&] {
        world.view<Position>().each([](ecs::Entity /*entity*/, Position& position) { position.y = 0.0F; });
    });
    timed("remove Frozen from all", [&] {
        for (u32 i = 0; i < count; i += 10) {
            world.remove<Frozen>(entities[i]);
        }
    });
    timed("destroy all", [&] {
        for (const ecs::Entity entity : entities) {
            world.destroy(entity);
        }
    });

    TESERRA_LOG_INFO(LogCategory::App, "{} entities moved, total health {:.0f}, {} left", moved, totalHealth,
                     world.entityCount());
    log::flush();
    return world.entityCount() == 0 ? 0 : 1;
}
