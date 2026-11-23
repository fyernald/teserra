#pragma once

// Deferred structural changes (docs/architecture/ecs.md#structural-changes-commands).

#include <teserra/core/assert.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/component_meta.hpp>
#include <teserra/ecs/entity.hpp>
#include <teserra/ecs/type_key.hpp>
#include <teserra/ecs/world.hpp>

#include <cstddef>
#include <memory>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace teserra::ecs {

/// Records creates, destroys, adds and removes, and applies them to its World later, in recording order.
/// Systems use it to change the World's structure while a view or forEachEntity() iterates, where direct
/// changes assert; the Schedule applies it at sync points (iteration 10, #75).
///
///     ecs::Commands commands(world);
///     for (auto [entity, gun, transform] : world.view<Gun, const Transform>()) {
///         const ecs::Entity bullet = commands.create();     // usable in this buffer right away
///         commands.add<Transform>(bullet, transform);
///         commands.add<Velocity>(bullet, gun.direction * gun.muzzleSpeed);
///     }
///     commands.apply();                                     // after the loop: the views' locks are gone
///
/// Components are constructed when they are recorded and moved into the World when applied. The storage is
/// kept between frames, so recording and applying allocate nothing in steady state. A Commands must not
/// outlive its World, nor be used after the World is moved. Not thread-safe.
class Commands {
public:
    /// The largest component alignment that Commands supports.
    static constexpr usize kMaxAlignment = 64;

    explicit Commands(World& world) noexcept
        : m_world(&world) {}

    /// Discards what was not applied.
    ~Commands() { clear(); }

    Commands(const Commands&) = delete;
    Commands& operator=(const Commands&) = delete;
    Commands(Commands&&) = delete;
    Commands& operator=(Commands&&) = delete;

    /// A reserved entity: the one World::create() would have returned. Commands of this buffer can use it
    /// right away; it becomes alive when the buffer is applied, and stale if the buffer is cleared first.
    [[nodiscard]] Entity create() {
        const Entity entity = m_world->reserve();
        m_records.push_back({.operation = Operation::Create, .entity = entity});
        return entity;
    }

    /// Destroys `entity` and its components when applied.
    void destroy(Entity entity) {
        requireUsable(entity, "destroy", {});
        m_records.push_back({.operation = Operation::Destroy, .entity = entity});
    }

    /// Gives `entity` a T, constructed from `args` now, when applied.
    template <Component T, typename... Args>
        requires(!std::is_empty_v<T>)
    void add(Entity entity, Args&&... args) {
        static_assert(alignof(T) <= kMaxAlignment,
                      "Commands supports components aligned to at most 64 bytes");
        requireUsable(entity, "add", componentName<T>());
        void* payload = allocate(sizeof(T), alignof(T));
        ::new (payload) T(std::forward<Args>(args)...);
        m_records.push_back({.operation = Operation::Add,
                             .entity = entity,
                             .apply = &applyAdd<T>,
                             .discard = &discardPayload<T>,
                             .payload = payload});
    }

    /// Gives `entity` the tag T when applied.
    template <Component T>
        requires std::is_empty_v<T>
    void add(Entity entity) {
        requireUsable(entity, "add", componentName<T>());
        m_records.push_back({.operation = Operation::Add, .entity = entity, .apply = &applyAddTag<T>});
    }

    /// Removes T from `entity` when applied.
    template <Component T>
    void remove(Entity entity) {
        requireUsable(entity, "remove", componentName<T>());
        m_records.push_back({.operation = Operation::Remove, .entity = entity, .apply = &applyRemove<T>});
    }

    /// Applies the commands in recording order, then clears the buffer and keeps its storage. Each command
    /// has the checks of the World function it calls: adding a component twice, removing a missing one,
    /// destroying a dead entity, or changing a pool that a view is iterating asserts.
    void apply();

    /// Discards the commands that were not applied: destroys their recorded components and releases their
    /// reserved entities. Keeps the storage. Not noexcept: releasing an entity during forEachEntity()
    /// asserts, and test assert handlers throw.
    void clear();

    /// The number of recorded commands.
    [[nodiscard]] usize size() const noexcept { return m_records.size() - m_next; }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    [[nodiscard]] World& world() const noexcept { return *m_world; }

private:
    enum class Operation : u8 {
        Create,
        Destroy,
        Add,
        Remove
    };

    struct Record {
        Operation operation = Operation::Create;
        Entity entity;
        // Add and Remove: the World call, instantiated for the component type.
        void (*apply)(World& world, Entity entity, void* payload) = nullptr;
        // Add of a data component: destroys the payload. apply() also destroys it after moving from it.
        void (*discard)(void* payload) noexcept = nullptr;
        void* payload = nullptr;
    };

    // Payload chunks never move, so components that are not trivially relocatable (std::string) stay valid.
    // A chunk is raw storage from an aligned operator new; the pointer is to its first byte.
    struct ChunkDeleter {
        void operator()(std::byte* chunk) const noexcept {
            ::operator delete(chunk, std::align_val_t{kMaxAlignment});
        }
    };
    struct Chunk {
        std::unique_ptr<std::byte, ChunkDeleter> memory;
        usize size = 0;
    };
    static constexpr usize kChunkSize = usize{16} * 1024;

    template <typename T>
    static void applyAdd(World& world, Entity entity, void* payload) {
        world.add<T>(entity, std::move(*static_cast<T*>(payload)));
    }
    template <typename T>
    static void applyAddTag(World& world, Entity entity, void* /*payload*/) {
        world.add<T>(entity);
    }
    template <typename T>
    static void applyRemove(World& world, Entity entity, void* /*payload*/) {
        world.remove<T>(entity);
    }
    template <typename T>
    static void discardPayload(void* payload) noexcept {
        static_cast<T*>(payload)->~T();
    }

    // Asserts that `entity` is alive or reserved in this buffer's World. `operation` and `component` name the
    // command in the message.
    void requireUsable([[maybe_unused]] Entity entity, [[maybe_unused]] std::string_view operation,
                       [[maybe_unused]] std::string_view component) const {
        TESERRA_ASSERT(m_world->isAlive(entity) || m_world->isReserved(entity),
                       "Commands::{}({}) for entity {}, which is neither alive nor reserved", operation,
                       component, entity);
    }

    // Storage for one payload in the current chunk, or the next one, which is allocated if needed.
    void* allocate(usize size, usize alignment);

    World* m_world;
    std::vector<Record> m_records;
    usize m_next = 0; // the first record not applied yet
    std::vector<Chunk> m_chunks;
    usize m_chunk = 0;  // the chunk payloads are placed in
    usize m_offset = 0; // the first free byte in it
};

} // namespace teserra::ecs
