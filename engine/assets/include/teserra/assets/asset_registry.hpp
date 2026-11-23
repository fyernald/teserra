#pragma once

// Typed, reference-counted asset handles and the registry that owns the assets
// (docs/architecture/assets.md#handles-and-ids, ADR-0020).
//
//   AssetRegistry assets;
//   Asset<ImageData> hero = assets.load<ImageData>("assets://sprites/hero.png", [&] { return decode(); });
//   if (hero.isReady()) { use(hero.get()); }
//   Asset<ImageData> again = assets.load<ImageData>("assets://sprites/hero.png", ...);  // same slot
//   hero = {};  again = {};        // no references left: unloaded at the next endFrame()
//   assets.endFrame();
//
// Loading a path that is loaded already returns its slot without calling `create`. A failed `create` is
// logged once and leaves a failed slot (isReady() is false), deduplicated like a loaded one. Copying an
// Asset<T> adds a reference and destroying one removes it; a slot without references is unloaded by the
// next endFrame(), so an asset dropped and loaded again within one frame is not loaded twice. Not
// thread-safe: loading is single-threaded in v1.

#include <teserra/assets/asset_id.hpp>
#include <teserra/core/assert.hpp>
#include <teserra/core/handle.hpp>
#include <teserra/core/handle_pool.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>

#include <concepts>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace teserra::assets {

/// The registry's slot of an asset. Asset<T> wraps it with a type and a reference.
using AssetHandle = Handle<struct AssetTag>;

class AssetRegistry;

namespace detail {

// The address of assetTypeTag<T> identifies T within the program, without RTTI. The variable is writable
// on purpose: linkers may fold identical read-only constants into one address, but never writable ones.
template <typename T>
inline char assetTypeTag = 0;

using AssetTypeId = const void*;

template <typename T>
[[nodiscard]] AssetTypeId assetTypeId() noexcept {
    return &assetTypeTag<T>;
}

// An object of any type with the function that destroys it.
class ErasedObject {
public:
    ErasedObject() noexcept = default;

    template <typename T>
    [[nodiscard]] static ErasedObject make(T&& value) {
        using Object = std::remove_cvref_t<T>;
        ErasedObject erased;
        erased.m_object = new Object(std::forward<T>(value)); // NOLINT(cppcoreguidelines-owning-memory)
        erased.m_destroy = [](void* object) noexcept {
            delete static_cast<Object*>(object); // NOLINT(cppcoreguidelines-owning-memory)
        };
        return erased;
    }

    ErasedObject(ErasedObject&& other) noexcept
        : m_object(std::exchange(other.m_object, nullptr)),
          m_destroy(std::exchange(other.m_destroy, nullptr)) {}

    ErasedObject& operator=(ErasedObject&& other) noexcept {
        if (this != &other) {
            reset();
            m_object = std::exchange(other.m_object, nullptr);
            m_destroy = std::exchange(other.m_destroy, nullptr);
        }
        return *this;
    }

    ErasedObject(const ErasedObject&) = delete;
    ErasedObject& operator=(const ErasedObject&) = delete;

    ~ErasedObject() { reset(); }

    [[nodiscard]] void* get() const noexcept { return m_object; }

private:
    void reset() noexcept {
        if (m_object != nullptr) {
            m_destroy(std::exchange(m_object, nullptr));
        }
    }

    void* m_object = nullptr;
    void (*m_destroy)(void*) noexcept = nullptr;
};

} // namespace detail

/// A shared reference to an asset of type T in an AssetRegistry. The registry must outlive it.
template <typename T>
class Asset {
public:
    /// A null asset: refers to nothing, never ready.
    Asset() noexcept = default;

    Asset(const Asset& other) noexcept;
    Asset(Asset&& other) noexcept
        : m_registry(std::exchange(other.m_registry, nullptr)),
          m_handle(std::exchange(other.m_handle, AssetHandle{})) {}
    Asset& operator=(const Asset& other) noexcept;
    Asset& operator=(Asset&& other) noexcept;
    ~Asset();

    [[nodiscard]] bool isNull() const noexcept { return m_registry == nullptr; }

    /// Whether the asset loaded: false for a null asset and for a failed load.
    [[nodiscard]] bool isReady() const noexcept;

    /// The asset. Asserts isReady().
    [[nodiscard]] const T& get() const;

    /// The ID of the asset's path; null for a null asset.
    [[nodiscard]] AssetId id() const noexcept;

    /// The registry slot; null for a null asset.
    [[nodiscard]] AssetHandle handle() const noexcept { return m_handle; }

    /// Two assets are equal when they refer to the same slot.
    friend bool operator==(const Asset& left, const Asset& right) noexcept {
        return left.m_handle == right.m_handle;
    }

private:
    friend class AssetRegistry;

    // Adopts one reference the registry counted already.
    Asset(AssetRegistry& registry, AssetHandle handle) noexcept
        : m_registry(&registry), m_handle(handle) {}

    AssetRegistry* m_registry = nullptr;
    AssetHandle m_handle{};
};

/// Options of an AssetRegistry.
struct AssetRegistryConfig {
    /// Hashes a normalized path into an AssetId value. Null uses FNV-1a (AssetId::fromPath()); tests
    /// inject collisions with it.
    u64 (*hashPath)(std::string_view normalizedPath) = nullptr;
};

/// Owns the assets, deduplicated by path, and unloads them once nothing refers to them.
class AssetRegistry {
public:
    explicit AssetRegistry(AssetRegistryConfig config = {});

    /// Unloads what nothing refers to and asserts that no Asset<T> of this registry is left.
    ~AssetRegistry();

    // Asset<T>s point to their registry.
    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;
    AssetRegistry(AssetRegistry&&) = delete;
    AssetRegistry& operator=(AssetRegistry&&) = delete;

    /// The asset at `virtualPath`. If the path is loaded already (or failed already), returns its slot
    /// with one more reference; otherwise calls `create` once, which returns Result<T>. A failure is logged
    /// with the path and gives an asset that is not ready. Asserts if the path was loaded as another type,
    /// or if another path has the same AssetId. `create` may load other assets (dependencies).
    template <typename T, typename Create>
        requires std::invocable<Create&> && std::same_as<std::invoke_result_t<Create&>, Result<T>>
    [[nodiscard]] Asset<T> load(std::string_view virtualPath, Create&& create);

    /// Unloads the assets nothing refers to. The App calls it at the end of each frame.
    void endFrame();

    /// The normalized path of a loaded or failed asset; empty when `id` is unknown.
    [[nodiscard]] std::string_view path(AssetId id) const noexcept;

    /// Loaded and failed assets, including those waiting for endFrame() to unload them.
    [[nodiscard]] usize size() const noexcept { return m_slots.size(); }

private:
    template <typename T>
    friend class Asset;

    struct Slot {
        AssetId id;
        std::string path;
        detail::AssetTypeId type = nullptr;
        detail::ErasedObject object; // empty when the load failed
        u32 references = 0;
        bool queued = false; // listed for endFrame(), at most once
    };

    [[nodiscard]] AssetId idOf(std::string_view normalizedPath) const noexcept;
    // The live slot of `id` with one more reference, or a null handle. Asserts on a collision or another
    // type.
    [[nodiscard]] AssetHandle acquire(AssetId id, std::string_view normalizedPath, detail::AssetTypeId type);
    // A new slot with one reference.
    [[nodiscard]] AssetHandle insert(AssetId id, std::string normalizedPath, detail::AssetTypeId type,
                                     detail::ErasedObject object);
    void logFailure(std::string_view normalizedPath, const Error& error) const;

    void retain(AssetHandle handle) noexcept;
    void release(AssetHandle handle) noexcept;
    [[nodiscard]] bool isReady(AssetHandle handle) const noexcept;
    [[nodiscard]] AssetId idOf(AssetHandle handle) const noexcept;
    [[nodiscard]] const void* object(AssetHandle handle, detail::AssetTypeId type) const;

    AssetRegistryConfig m_config;
    HandlePool<Slot, AssetTag> m_slots;
    std::unordered_map<AssetId, AssetHandle> m_byId;
    // Slots whose count reached zero since the last endFrame(), and endFrame()'s working list. A slot is in
    // at most one of them, once, so insert() reserves a place in each for every slot and release() never
    // allocates.
    std::vector<AssetHandle> m_unreferenced;
    std::vector<AssetHandle> m_unloading;
};

template <typename T, typename Create>
    requires std::invocable<Create&> && std::same_as<std::invoke_result_t<Create&>, Result<T>>
Asset<T> AssetRegistry::load(std::string_view virtualPath, Create&& create) {
    std::string normalized = normalizeAssetPath(virtualPath);
    const AssetId id = idOf(normalized);
    const detail::AssetTypeId type = detail::assetTypeId<T>();
    if (const AssetHandle loaded = acquire(id, normalized, type); !loaded.isNull()) {
        return Asset<T>(*this, loaded);
    }
    Result<T> created = std::invoke(create);
    detail::ErasedObject object;
    if (created) {
        object = detail::ErasedObject::make(std::move(*created));
    } else {
        logFailure(normalized, created.error());
    }
    return Asset<T>(*this, insert(id, std::move(normalized), type, std::move(object)));
}

template <typename T>
Asset<T>::Asset(const Asset& other) noexcept
    : m_registry(other.m_registry), m_handle(other.m_handle) {
    if (m_registry != nullptr) {
        m_registry->retain(m_handle);
    }
}

template <typename T>
Asset<T>& Asset<T>::operator=(const Asset& other) noexcept {
    if (this == &other) {
        return *this;
    }
    Asset copy(other);
    std::swap(m_registry, copy.m_registry);
    std::swap(m_handle, copy.m_handle);
    return *this;
}

template <typename T>
Asset<T>& Asset<T>::operator=(Asset&& other) noexcept {
    Asset moved(std::move(other));
    std::swap(m_registry, moved.m_registry);
    std::swap(m_handle, moved.m_handle);
    return *this;
}

template <typename T>
Asset<T>::~Asset() {
    if (m_registry != nullptr) {
        m_registry->release(m_handle);
    }
}

template <typename T>
bool Asset<T>::isReady() const noexcept {
    return m_registry != nullptr && m_registry->isReady(m_handle);
}

template <typename T>
const T& Asset<T>::get() const {
    TESERRA_ASSERT(m_registry != nullptr, "Asset::get() on a null asset");
    return *static_cast<const T*>(m_registry->object(m_handle, detail::assetTypeId<T>()));
}

template <typename T>
AssetId Asset<T>::id() const noexcept {
    return m_registry != nullptr ? m_registry->idOf(m_handle) : AssetId{};
}

} // namespace teserra::assets
