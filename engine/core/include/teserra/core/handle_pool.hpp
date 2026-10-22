#pragma once

// Storage for objects referred to by generational handles (ADR-0020).
//
//   HandlePool<GlBuffer, BufferTag> buffers;
//   const BufferHandle handle = buffers.create(id, size);
//   buffers.get(handle).size;      // asserts that the handle is live
//   buffers.destroy(handle);
//   buffers.tryGet(handle);        // nullptr: the handle is stale now
//
// Objects live in a slot array. Each slot has a generation that is part of the handles to it and is
// incremented when its object is destroyed, so old handles stop matching. Freed slots are reused in
// FIFO order, which keeps a stale handle detectable for as long as possible. A slot whose generation
// reaches MaxGeneration is retired instead of reused, so a generation never wraps around.

#include <teserra/core/assert.hpp>
#include <teserra/core/handle.hpp>
#include <teserra/core/types.hpp>

#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace teserra {

/// Owns objects of type T and hands out Handle<Tag> to them. Not thread-safe. create() may reallocate
/// the slot array, which invalidates references and pointers returned by get() and tryGet() (handles
/// stay valid).
template <typename T, typename Tag, u32 MaxGeneration = std::numeric_limits<u32>::max()>
class HandlePool {
    static_assert(MaxGeneration >= 1, "generation 0 is the null handle");

public:
    using HandleType = Handle<Tag>;

    /// Constructs an object from `args` and returns its handle.
    template <typename... Args>
    [[nodiscard]] HandleType create(Args&&... args) {
        u32 index = 0;
        if (m_freeHead != kNoSlot) {
            index = m_freeHead;
            m_freeHead = m_slots[index].nextFree;
            if (m_freeHead == kNoSlot) {
                m_freeTail = kNoSlot;
            }
        } else {
            TESERRA_ASSERT(m_slots.size() < kNoSlot, "HandlePool is full ({} slots)", m_slots.size());
            index = static_cast<u32>(m_slots.size());
            m_slots.emplace_back();
        }
        Slot& slot = m_slots[index];
        slot.object.emplace(std::forward<Args>(args)...);
        slot.nextFree = kNoSlot;
        ++m_size;
        return HandleType(index, slot.generation);
    }

    /// Destroys the object behind `handle`, which must be live (asserted; ignored when assertions are
    /// off). The handle and all its copies become stale.
    void destroy(HandleType handle) {
        TESERRA_ASSERT(contains(handle), "Destroying stale or null handle {}", handle);
        if (!contains(handle)) {
            return;
        }
        TESERRA_ASSERT(m_iterating == 0, "HandlePool::destroy() during forEach()");
        const u32 index = handle.index();
        Slot& slot = m_slots[index];
        slot.object.reset();
        --m_size;
        if (slot.generation == MaxGeneration) {
            ++m_retired; // never reused: its next generation would wrap around
            return;
        }
        ++slot.generation;
        if (m_freeTail == kNoSlot) {
            m_freeHead = index;
        } else {
            m_slots[m_freeTail].nextFree = index;
        }
        m_freeTail = index;
    }

    /// Destroys every object. All handles handed out so far become stale.
    void clear() {
        for (u32 index = 0; index < m_slots.size(); ++index) {
            if (m_slots[index].object.has_value()) {
                destroy(HandleType(index, m_slots[index].generation));
            }
        }
    }

    /// True if `handle` refers to a live object of this pool.
    [[nodiscard]] bool contains(HandleType handle) const noexcept { return find(*this, handle) != nullptr; }

    /// The object behind `handle`, which must be live (asserted).
    [[nodiscard]] T& get(HandleType handle) {
        T* const object = tryGet(handle);
        TESERRA_ASSERT(object != nullptr, "Stale or null handle {}", handle);
        return *object;
    }

    [[nodiscard]] const T& get(HandleType handle) const {
        const T* const object = tryGet(handle);
        TESERRA_ASSERT(object != nullptr, "Stale or null handle {}", handle);
        return *object;
    }

    /// The object behind `handle`, or nullptr if the handle is null or stale.
    [[nodiscard]] T* tryGet(HandleType handle) noexcept { return find(*this, handle); }
    [[nodiscard]] const T* tryGet(HandleType handle) const noexcept { return find(*this, handle); }

    /// Calls `function(handle, object)` for every live object, in slot order. The function must not
    /// create or destroy objects of this pool (destroying is asserted).
    template <typename Function>
    void forEach(Function&& function) {
        const IterationScope scope(m_iterating);
        for (u32 index = 0; index < m_slots.size(); ++index) {
            if (m_slots[index].object.has_value()) {
                function(HandleType(index, m_slots[index].generation), *m_slots[index].object);
            }
        }
    }

    template <typename Function>
    void forEach(Function&& function) const {
        for (u32 index = 0; index < m_slots.size(); ++index) {
            if (m_slots[index].object.has_value()) {
                function(HandleType(index, m_slots[index].generation), std::as_const(*m_slots[index].object));
            }
        }
    }

    /// Number of live objects.
    [[nodiscard]] usize size() const noexcept { return m_size; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

    /// Number of slots retired because their generation reached MaxGeneration.
    [[nodiscard]] usize retiredSlots() const noexcept { return m_retired; }

    /// Reserves slots for `count` objects in total, so warm-up can avoid reallocation later.
    void reserve(usize count) { m_slots.reserve(count); }

private:
    static constexpr u32 kNoSlot = std::numeric_limits<u32>::max();

    struct Slot {
        std::optional<T> object;
        u32 generation = 1;     // of the current or next object in this slot
        u32 nextFree = kNoSlot; // FIFO free list, valid while the slot is free
    };

    // Counts nested forEach() calls, also when the function throws (test assert handlers do).
    class IterationScope {
    public:
        explicit IterationScope(u32& depth) noexcept
            : m_depth(depth) {
            ++m_depth;
        }
        ~IterationScope() { --m_depth; }

        IterationScope(const IterationScope&) = delete;
        IterationScope& operator=(const IterationScope&) = delete;

    private:
        u32& m_depth;
    };

    // Shared by the const and non-const lookups: T* or const T* depending on `pool`.
    template <typename Pool>
    [[nodiscard]] static std::conditional_t<std::is_const_v<Pool>, const T*, T*>
    find(Pool& pool, HandleType handle) noexcept {
        if (handle.isNull() || handle.index() >= pool.m_slots.size()) {
            return nullptr;
        }
        auto& slot = pool.m_slots[handle.index()];
        if (slot.generation != handle.generation() || !slot.object.has_value()) {
            return nullptr;
        }
        return &*slot.object;
    }

    std::vector<Slot> m_slots;
    usize m_size = 0;
    usize m_retired = 0;
    u32 m_freeHead = kNoSlot;
    u32 m_freeTail = kNoSlot;
    u32 m_iterating = 0; // nesting depth of forEach(), for the destroy() assertion
};

} // namespace teserra
