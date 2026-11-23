#include <teserra/ecs/schedule.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/core/log.hpp>

#include <algorithm>
#include <utility>

namespace teserra::ecs {

std::string_view phaseName(Phase phase) noexcept {
    switch (phase) {
        case Phase::Startup:
            return "Startup";
        case Phase::PreUpdate:
            return "PreUpdate";
        case Phase::FixedUpdate:
            return "FixedUpdate";
        case Phase::Update:
            return "Update";
        case Phase::PostUpdate:
            return "PostUpdate";
        case Phase::Extract:
            return "Extract";
        case Phase::Last:
            return "Last";
    }
    return "Unknown";
}

SystemConfig& SystemConfig::before(std::string_view name) {
    m_schedule->m_systems[m_index].before.emplace_back(name);
    m_schedule->m_dirty = true;
    return *this;
}

SystemConfig& SystemConfig::after(std::string_view name) {
    m_schedule->m_systems[m_index].after.emplace_back(name);
    m_schedule->m_dirty = true;
    return *this;
}

SystemAccess& SystemConfig::access() {
    return m_schedule->m_systems[m_index].access;
}

Schedule::Schedule() = default;
Schedule::~Schedule() = default;

SystemConfig Schedule::add(Phase phase, std::string name, SystemFunction function) {
    TESERRA_ASSERT(!m_running, "Schedule::add() while the schedule runs");
    TESERRA_ASSERT(std::ranges::none_of(m_systems, [&](const System& system) { return system.name == name; }),
                   "A system named '{}' already exists", name);
    m_systems.push_back({.phase = phase,
                         .name = std::move(name),
                         .function = std::move(function),
                         .access = {},
                         .before = {},
                         .after = {}});
    m_dirty = true;
    return {*this, m_systems.size() - 1};
}

Result<void> Schedule::build() {
    for (usize phase = 0; phase < kPhaseCount; ++phase) {
        if (Result<void> built = buildPhase(static_cast<Phase>(phase)); !built) {
            return built;
        }
    }
    m_dirty = false;
    return {};
}

Result<void> Schedule::buildPhase(Phase phase) {
    // The phase's systems in registration order; positions below index into `members`.
    std::vector<usize> members;
    for (usize index = 0; index < m_systems.size(); ++index) {
        if (m_systems[index].phase == phase) {
            members.push_back(index);
        }
    }
    const auto positionOf = [&](std::string_view name) -> usize {
        const auto found =
            std::ranges::find_if(members, [&](usize index) { return m_systems[index].name == name; });
        return static_cast<usize>(found - members.begin());
    };

    // successors[a] holds b and predecessors[b] holds a for every "a runs before b".
    std::vector<std::vector<usize>> successors(members.size());
    std::vector<std::vector<usize>> predecessors(members.size());
    std::vector<usize> predecessorCount(members.size(), 0);
    for (usize position = 0; position < members.size(); ++position) {
        const System& system = m_systems[members[position]];
        const auto link = [&](usize from, usize to) {
            successors[from].push_back(to);
            predecessors[to].push_back(from);
            ++predecessorCount[to];
        };
        for (const std::string& name : system.before) {
            const usize other = positionOf(name);
            if (other == members.size()) {
                return fail(ErrorCode::InvalidArgument, Module::Ecs,
                            "System '{}' in {} runs before '{}', which is not in that phase", system.name,
                            phaseName(phase), name);
            }
            link(position, other);
        }
        for (const std::string& name : system.after) {
            const usize other = positionOf(name);
            if (other == members.size()) {
                return fail(ErrorCode::InvalidArgument, Module::Ecs,
                            "System '{}' in {} runs after '{}', which is not in that phase", system.name,
                            phaseName(phase), name);
            }
            link(other, position);
        }
    }

    // Kahn's algorithm, always taking the earliest registered system that is ready, so that systems without
    // constraints keep their registration order.
    std::vector<usize> order;
    std::vector<bool> placed(members.size(), false);
    while (order.size() < members.size()) {
        usize next = members.size();
        for (usize position = 0; position < members.size(); ++position) {
            if (!placed[position] && predecessorCount[position] == 0) {
                next = position;
                break;
            }
        }
        if (next == members.size()) {
            // Every remaining system waits for another remaining one, so walking from one of them to a
            // remaining predecessor, again and again, must repeat a system: the walk has entered a cycle.
            // Name the last two systems of the walk, which are both on it. (Successors would not do: a system
            // after a cycle may have none left.)
            std::vector<bool> visited(members.size(), false);
            usize current = static_cast<usize>(std::ranges::find(placed, false) - placed.begin());
            usize previous = current;
            while (!visited[current]) {
                visited[current] = true;
                previous = current;
                current =
                    *std::ranges::find_if(predecessors[current], [&](usize from) { return !placed[from]; });
            }
            // `current` runs before `previous`.
            return fail(
                ErrorCode::InvalidArgument, Module::Ecs, "Systems '{}' and '{}' form an ordering cycle in {}",
                m_systems[members[current]].name, m_systems[members[previous]].name, phaseName(phase));
        }
        placed[next] = true;
        order.push_back(members[next]);
        for (const usize successor : successors[next]) {
            --predecessorCount[successor];
        }
    }
    m_order[static_cast<usize>(phase)] = std::move(order);
    return {};
}

void Schedule::ensureBuilt() {
    if (!m_dirty) {
        return;
    }
    if (Result<void> built = build(); !built) {
        TESERRA_ASSERT(false, "{}", built.error().message);
        TESERRA_LOG_ERROR(LogCategory::Ecs, "{}; running systems in registration order",
                          built.error().message);
        for (usize phase = 0; phase < kPhaseCount; ++phase) {
            m_order[phase].clear();
        }
        for (usize index = 0; index < m_systems.size(); ++index) {
            m_order[static_cast<usize>(m_systems[index].phase)].push_back(index);
        }
        m_dirty = false;
    }
}

Commands& Schedule::commandsFor(World& world) {
    if (!m_commands || &m_commands->world() != &world) {
        m_commands = std::make_unique<Commands>(world);
    }
    return *m_commands;
}

void Schedule::run(Phase phase, World& world) {
    TESERRA_ASSERT(!m_running, "Schedule::run() inside a system of the same schedule");
    ensureBuilt();
    Commands& commands = commandsFor(world);
    // Reset also when a system throws (test assert handlers do).
    class RunningScope {
    public:
        explicit RunningScope(bool& running) noexcept
            : m_flag(running) {
            m_flag = true;
        }
        RunningScope(const RunningScope&) = delete;
        RunningScope& operator=(const RunningScope&) = delete;
        RunningScope(RunningScope&&) = delete;
        RunningScope& operator=(RunningScope&&) = delete;
        ~RunningScope() { m_flag = false; }

    private:
        bool& m_flag;
    } scope(m_running);
    for (const usize index : m_order[static_cast<usize>(phase)]) {
        System& system = m_systems[index];
        SystemContext context(world, commands, system.name, system.access);
        system.function(context);
    }
    commands.apply(); // the sync point
}

std::vector<std::string_view> Schedule::order(Phase phase) {
    ensureBuilt();
    std::vector<std::string_view> names;
    for (const usize index : m_order[static_cast<usize>(phase)]) {
        names.emplace_back(m_systems[index].name);
    }
    return names;
}

} // namespace teserra::ecs
