#pragma once

// Systems in phases, ordered and with declared access (docs/architecture/ecs.md#systems-and-scheduling).

#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/commands.hpp>
#include <teserra/ecs/events.hpp>
#include <teserra/ecs/system_context.hpp>
#include <teserra/ecs/world.hpp>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace teserra::ecs {

/// The phases of a frame, in the order the App runs them: Startup once, then every frame PreUpdate,
/// FixedUpdate once per fixed step (zero or more times), Update, PostUpdate, Extract (render extraction),
/// and Last after rendering.
enum class Phase : u8 {
    Startup,
    PreUpdate,
    FixedUpdate,
    Update,
    PostUpdate,
    Extract,
    Last
};

inline constexpr usize kPhaseCount = 7;

/// The name of `phase`, e.g. "Update".
[[nodiscard]] std::string_view phaseName(Phase phase) noexcept;

class Schedule;

/// Configures a system right after Schedule::add(): its data access and its order in the phase. Every
/// function returns *this, for chaining.
class SystemConfig {
public:
    /// The system reads components T (views with const T, or T excluded).
    template <Component T>
    SystemConfig& reads() {
        SystemAccess::insert(access().componentReads, typeKey<T>());
        return *this;
    }
    /// The system writes components T (views with T); implies reads<T>().
    template <Component T>
    SystemConfig& writes() {
        SystemAccess::insert(access().componentWrites, typeKey<T>());
        return *this;
    }
    template <Resource T>
    SystemConfig& readsResource() {
        SystemAccess::insert(access().resourceReads, typeKey<T>());
        return *this;
    }
    /// Implies readsResource<T>().
    template <Resource T>
    SystemConfig& writesResource() {
        SystemAccess::insert(access().resourceWrites, typeKey<T>());
        return *this;
    }

    /// The system reads events T (SystemContext::readEvents<T>()): readsResource<Events<T>>().
    template <typename T>
    SystemConfig& readsEvents() {
        return readsResource<Events<T>>();
    }
    /// The system sends events T (SystemContext::sendEvent()): writesResource<Events<T>>().
    template <typename T>
    SystemConfig& sendsEvents() {
        return writesResource<Events<T>>();
    }

    /// Runs before the system `name` of the same phase.
    SystemConfig& before(std::string_view name);
    /// Runs after the system `name` of the same phase.
    SystemConfig& after(std::string_view name);

private:
    friend class Schedule;
    SystemConfig(Schedule& schedule, usize index) noexcept
        : m_schedule(&schedule), m_index(index) {}

    [[nodiscard]] SystemAccess& access();

    Schedule* m_schedule;
    usize m_index;
};

/// Systems grouped by phase and run in order on one thread. Within a phase, systems run in registration
/// order, adjusted by before()/after(); cycles and unknown names are configuration errors (build()). Each
/// system declares what it reads and writes, and SystemContext asserts on anything else in debug builds.
/// Structural changes go through commands, applied after each phase (after each FixedUpdate step).
///
///     schedule.add(Phase::Update, "player_movement", playerMovement)
///         .reads<Player>().writes<Transform>().readsResource<Time>()
///         .after("input_mapping");
///     schedule.run(Phase::Update, world);
class Schedule {
public:
    using SystemFunction = std::function<void(SystemContext& context)>;

    Schedule();
    ~Schedule();

    Schedule(const Schedule&) = delete;
    Schedule& operator=(const Schedule&) = delete;
    Schedule(Schedule&&) = delete;
    Schedule& operator=(Schedule&&) = delete;

    /// Adds a system to `phase`. Names are unique in the schedule (asserted). Not while the schedule runs
    /// (asserted).
    SystemConfig add(Phase phase, std::string name, SystemFunction function);

    /// Orders every phase. Fails if constraints form a cycle (naming two systems on it) or name a system
    /// that is not in the same phase.
    [[nodiscard]] Result<void> build();

    /// Runs the systems of `phase` on `world` in order, then applies their commands. Builds first if
    /// systems were added; a build error asserts, and with assertions off it is logged once and the phase
    /// runs in registration order. `world` must outlive the schedule or the next run on another world.
    void run(Phase phase, World& world);

    /// The names of the systems of `phase` in the order they run. Builds first, like run().
    [[nodiscard]] std::vector<std::string_view> order(Phase phase);

    [[nodiscard]] usize systemCount() const noexcept { return m_systems.size(); }

private:
    friend class SystemConfig;

    struct System {
        Phase phase = Phase::Update;
        std::string name;
        SystemFunction function;
        SystemAccess access{};
        std::vector<std::string> before{};
        std::vector<std::string> after{};
        EventCursors cursors{};
    };

    void ensureBuilt();
    // Orders one phase into m_order; fails on cycles and unknown names.
    [[nodiscard]] Result<void> buildPhase(Phase phase);
    // The Commands of `world`, recreated when the schedule runs another world.
    [[nodiscard]] Commands& commandsFor(World& world);

    std::vector<System> m_systems;
    std::array<std::vector<usize>, kPhaseCount> m_order{};
    bool m_dirty = false;
    bool m_running = false;
    std::unique_ptr<Commands> m_commands;
};

} // namespace teserra::ecs
