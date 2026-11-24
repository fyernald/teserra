#pragma once

// Typed, reference-counted asset handles, the loaders that produce assets and the registry that owns them
// (docs/architecture/assets.md#handles-and-ids, docs/architecture/assets.md#loaders, ADR-0020).
//
//   AssetRegistry assets{{.mounts = &mounts}};
//   assets.registerLoader<ImageData>(std::make_unique<ImageLoader>());
//   Asset<ImageData> hero = assets.load<ImageData>("assets://sprites/hero.png");   // read and decoded
//   Asset<ImageData> made = assets.load<ImageData>("game://noise", [&] { return makeNoise(); });
//   if (hero.isReady()) { use(hero.get()); }
//   Asset<ImageData> again = assets.load<ImageData>("assets://sprites/hero.png", ...);  // same slot
//   hero = {};  again = {};        // no references left: unloaded at the next endFrame()
//   assets.endFrame();
//
// Loading a path that is loaded already returns its slot without calling `create`. A failed `create` is
// logged once and leaves a failed slot, deduplicated like a loaded one: it resolves to the fallback of its
// type (setFallback()), or is not ready without one. Copying an
// Asset<T> adds a reference and destroying one removes it; a slot without references is unloaded by the
// next endFrame(), so an asset dropped and loaded again within one frame is not loaded twice. Not
// thread-safe: loading is single-threaded in v1.

#include <teserra/assets/asset_id.hpp>
#include <teserra/assets/mount_table.hpp>
#include <teserra/core/assert.hpp>
#include <teserra/core/handle.hpp>
#include <teserra/core/handle_pool.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>

#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace teserra::assets {

/// The registry's slot of an asset. Asset<T> wraps it with a type and a reference.
using AssetHandle = Handle<struct AssetTag>;

template <typename T>
class Asset;
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

// What the registry knows of an AssetLoader<T>: something it owns and destroys.
class AssetLoaderBase {
public:
    virtual ~AssetLoaderBase() = default;

protected:
    AssetLoaderBase() = default;
    AssetLoaderBase(const AssetLoaderBase&) = default;
    AssetLoaderBase& operator=(const AssetLoaderBase&) = default;
    AssetLoaderBase(AssetLoaderBase&&) = default;
    AssetLoaderBase& operator=(AssetLoaderBase&&) = default;
};

} // namespace detail

/// What a loader can do while it loads an asset: load the assets it depends on and read other files. Only
/// valid during AssetLoader::load().
class AssetLoadContext {
public:
    /// The normalized virtual path of the asset being loaded.
    [[nodiscard]] std::string_view path() const noexcept { return m_path; }

    /// A dependency, loaded through the registry like AssetRegistry::load<T>(path): a material's textures.
    /// The asset being loaded may keep it.
    template <typename T>
    [[nodiscard]] Asset<T> load(std::string_view virtualPath);

    /// The whole content of another file, read through the registry's mount points.
    [[nodiscard]] Result<std::vector<std::byte>> readFile(std::string_view virtualPath) const;

private:
    friend class AssetRegistry;

    AssetLoadContext(AssetRegistry& registry, std::string_view path) noexcept
        : m_registry(&registry), m_path(path) {}

    AssetRegistry* m_registry;
    std::string_view m_path;
};

/// Produces assets of type T from the bytes of their files. Registered per type with
/// AssetRegistry::registerLoader() and chosen by the extension of the path, so a new asset type only
/// needs a loader, not a change to the registry.
template <typename T>
class AssetLoader : public detail::AssetLoaderBase {
public:
    /// The file extensions this loader reads, lowercase and without the dot: {"png"}. The span must stay
    /// valid while the loader lives.
    [[nodiscard]] virtual std::span<const std::string_view> extensions() const = 0;

    /// The asset in `bytes`, the whole content of the file at context.path(). The registry logs a failure
    /// once, with the path, so the loader's errors do not need to name it.
    [[nodiscard]] virtual Result<T> load(AssetLoadContext& context, std::span<const std::byte> bytes) = 0;
};

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

    /// Whether get() may be called: the asset loaded, or its load failed and a fallback is bound. False for a
    /// null asset and for a failed load of a type without a fallback.
    [[nodiscard]] bool isReady() const noexcept;

    /// Whether the asset itself loaded (not a fallback).
    [[nodiscard]] bool isLoaded() const noexcept;

    /// Whether the load failed and get() returns the fallback of the type.
    [[nodiscard]] bool isFallback() const noexcept { return isReady() && !isLoaded(); }

    /// The asset, or the fallback its failed load resolved to. Asserts isReady().
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
    /// Where load<T>(path) reads files; it must outlive the registry. Null: only load<T>(path, create)
    /// works.
    const MountTable* mounts = nullptr;
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

    /// Makes `loader` load the assets of type T in files with its extensions. Asserts if another loader of
    /// T reads one of them, or if an extension is empty, or has a dot or an uppercase letter.
    template <typename T>
    void registerLoader(std::unique_ptr<AssetLoader<T>> loader);

    /// Makes the failed loads of T resolve to `fallback` (a magenta texture, an error shader), so that a
    /// missing file shows instead of crashing. Only paths that end with `pathSuffix` (compared without case)
    /// use it; "" matches every path, and the longest matching suffix wins. Failed slots that exist already
    /// are bound too. The registry owns the fallback until it is destroyed. Asserts if T has a fallback for
    /// `pathSuffix` already.
    template <typename T>
    void setFallback(T fallback, std::string_view pathSuffix = {});

    /// The asset at `virtualPath`, which the loader of T for the path's extension (compared without case)
    /// makes from the file's bytes, read through the mount points. Otherwise like load(path, create): loaded
    /// once and deduplicated, and a failure is logged once with the path and gives an asset that is not
    /// ready. Failures: no loader of T for the extension or no mount table (Unsupported), and the errors of
    /// MountTable::readFile() and of the loader.
    template <typename T>
    [[nodiscard]] Asset<T> load(std::string_view virtualPath);

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
    friend class AssetLoadContext;

    struct LoaderEntry {
        detail::AssetTypeId type = nullptr;
        std::span<const std::string_view> extensions;
        std::unique_ptr<detail::AssetLoaderBase> loader;
    };

    struct FallbackEntry {
        detail::AssetTypeId type = nullptr;
        std::string suffix; // lowercase
        detail::ErasedObject object;
    };

    struct Slot {
        AssetId id;
        std::string path;
        detail::AssetTypeId type = nullptr;
        detail::ErasedObject object;    // empty when the load failed
        const void* fallback = nullptr; // what a failed slot resolves to, owned by m_fallbacks; or null
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

    void addLoader(detail::AssetTypeId type, std::span<const std::string_view> extensions,
                   std::unique_ptr<detail::AssetLoaderBase> loader);
    // The loader of `type` for the extension of `virtualPath`, or Unsupported.
    [[nodiscard]] Result<detail::AssetLoaderBase*> findLoader(detail::AssetTypeId type,
                                                              std::string_view virtualPath) const;
    // Through the mount table, or Unsupported without one.
    [[nodiscard]] Result<std::vector<std::byte>> readFile(std::string_view virtualPath) const;

    void addFallback(detail::AssetTypeId type, std::string_view pathSuffix, detail::ErasedObject fallback);
    // The fallback object for a failed load of `type` at `normalizedPath`, or null.
    [[nodiscard]] const void* findFallback(detail::AssetTypeId type,
                                           std::string_view normalizedPath) const noexcept;

    void retain(AssetHandle handle) noexcept;
    void release(AssetHandle handle) noexcept;
    [[nodiscard]] bool isReady(AssetHandle handle) const noexcept;
    [[nodiscard]] bool isLoaded(AssetHandle handle) const noexcept;
    [[nodiscard]] AssetId idOf(AssetHandle handle) const noexcept;
    [[nodiscard]] const void* object(AssetHandle handle, detail::AssetTypeId type) const;

    AssetRegistryConfig m_config;
    // Before the slots, so that the loaders outlive the assets they made.
    std::vector<LoaderEntry> m_loaders;
    // Before the slots, which point to them. Each object is on the heap, so growing the vector moves none.
    std::vector<FallbackEntry> m_fallbacks;
    HandlePool<Slot, AssetTag> m_slots;
    std::unordered_map<AssetId, AssetHandle> m_byId;
    // Slots whose count reached zero since the last endFrame(), and endFrame()'s working list. A slot is in
    // at most one of them, once, so insert() reserves a place in each for every slot and release() never
    // allocates.
    std::vector<AssetHandle> m_unreferenced;
    std::vector<AssetHandle> m_unloading;
};

template <typename T>
void AssetRegistry::registerLoader(std::unique_ptr<AssetLoader<T>> loader) {
    TESERRA_ASSERT(loader != nullptr, "registerLoader() needs a loader");
    const std::span<const std::string_view> extensions = loader->extensions();
    addLoader(detail::assetTypeId<T>(), extensions, std::move(loader));
}

template <typename T>
void AssetRegistry::setFallback(T fallback, std::string_view pathSuffix) {
    addFallback(detail::assetTypeId<T>(), pathSuffix, detail::ErasedObject::make(std::move(fallback)));
}

template <typename T>
Asset<T> AssetRegistry::load(std::string_view virtualPath) {
    return load<T>(virtualPath, [this, virtualPath]() -> Result<T> {
        TESERRA_TRY_ASSIGN(detail::AssetLoaderBase* const loader,
                           findLoader(detail::assetTypeId<T>(), virtualPath));
        TESERRA_TRY_ASSIGN(const std::vector<std::byte> bytes, readFile(virtualPath));
        const std::string normalized = normalizeAssetPath(virtualPath);
        AssetLoadContext context(*this, normalized);
        return static_cast<AssetLoader<T>*>(loader)->load(context, bytes);
    });
}

template <typename T>
Asset<T> AssetLoadContext::load(std::string_view virtualPath) {
    return m_registry->load<T>(virtualPath);
}

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
bool Asset<T>::isLoaded() const noexcept {
    return m_registry != nullptr && m_registry->isLoaded(m_handle);
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
