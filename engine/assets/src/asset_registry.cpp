#include <teserra/assets/asset_registry.hpp>

#include <teserra/core/log.hpp>

#include <utility>

namespace teserra::assets {

AssetRegistry::AssetRegistry(AssetRegistryConfig config)
    : m_config(config) {
}

AssetRegistry::~AssetRegistry() {
    endFrame();
    // Asset<T>s left now would point to a destroyed registry. Name one of them to help find the owner.
    if (!m_slots.empty()) {
        std::string_view referenced;
        m_slots.forEach([&](AssetHandle, const Slot& slot) { referenced = slot.path; });
        TESERRA_ASSERT(false, "AssetRegistry destroyed while {} asset(s) are still referenced, e.g. '{}'",
                       m_slots.size(), referenced);
    }
}

void AssetRegistry::endFrame() {
    // Unloading an asset may release the assets it holds (a material's textures). Those land in
    // m_unreferenced and are unloaded by the next round, in this same call.
    while (!m_unreferenced.empty()) {
        std::swap(m_unreferenced, m_unloading);
        for (const AssetHandle handle : m_unloading) {
            Slot& slot = m_slots.get(handle);
            slot.queued = false;
            if (slot.references != 0) { // loaded again since it was released
                continue;
            }
            m_byId.erase(slot.id);
            m_slots.destroy(handle);
        }
        m_unloading.clear();
    }
}

std::string_view AssetRegistry::path(AssetId id) const noexcept {
    const auto found = m_byId.find(id);
    return found == m_byId.end() ? std::string_view{} : std::string_view(m_slots.get(found->second).path);
}

AssetId AssetRegistry::idOf(std::string_view normalizedPath) const noexcept {
    return m_config.hashPath != nullptr ? AssetId{m_config.hashPath(normalizedPath)}
                                        : AssetId::fromPath(normalizedPath);
}

AssetHandle AssetRegistry::acquire(AssetId id, std::string_view normalizedPath, detail::AssetTypeId type) {
    const auto found = m_byId.find(id);
    if (found == m_byId.end()) {
        return AssetHandle{};
    }
    Slot& slot = m_slots.get(found->second);
    TESERRA_ASSERT(slot.path == normalizedPath, "Asset paths '{}' and '{}' have the same AssetId {}",
                   slot.path, normalizedPath, id);
    TESERRA_ASSERT(slot.type == type, "Asset '{}' is loaded already as another type", slot.path);
    ++slot.references;
    return found->second;
}

AssetHandle AssetRegistry::insert(AssetId id, std::string normalizedPath, detail::AssetTypeId type,
                                  detail::ErasedObject object) {
    // `create` may have loaded other assets, but loading this one from inside its own `create` is a cycle.
    TESERRA_ASSERT(!m_byId.contains(id),
                   "Asset '{}' was loaded while it was being loaded (a dependency cycle)", normalizedPath);
    const AssetHandle handle = m_slots.create(Slot{.id = id,
                                                   .path = std::move(normalizedPath),
                                                   .type = type,
                                                   .object = std::move(object),
                                                   .references = 1});
    m_byId.emplace(id, handle);
    m_unreferenced.reserve(m_slots.size());
    m_unloading.reserve(m_slots.size());
    return handle;
}

void AssetRegistry::logFailure(std::string_view normalizedPath, const Error& error) const {
    TESERRA_LOG_ERROR(LogCategory::Assets, "Cannot load '{}': {}", normalizedPath, error);
}

void AssetRegistry::retain(AssetHandle handle) noexcept {
    ++m_slots.get(handle).references;
}

// push_back() cannot allocate here: insert() reserved a place for every slot.
void AssetRegistry::release(AssetHandle handle) noexcept { // NOLINT(bugprone-exception-escape)
    Slot& slot = m_slots.get(handle);
    TESERRA_ASSERT(slot.references > 0, "Asset '{}' released more often than referenced", slot.path);
    if (--slot.references == 0 && !slot.queued) {
        slot.queued = true;
        m_unreferenced.push_back(handle);
    }
}

bool AssetRegistry::isReady(AssetHandle handle) const noexcept {
    const Slot* const slot = m_slots.tryGet(handle);
    return slot != nullptr && slot->object.get() != nullptr;
}

AssetId AssetRegistry::idOf(AssetHandle handle) const noexcept {
    const Slot* const slot = m_slots.tryGet(handle);
    return slot != nullptr ? slot->id : AssetId{};
}

const void* AssetRegistry::object(AssetHandle handle, detail::AssetTypeId type) const {
    const Slot& slot = m_slots.get(handle);
    TESERRA_ASSERT(slot.type == type, "Asset '{}' read as another type than it was loaded as", slot.path);
    TESERRA_ASSERT(slot.object.get() != nullptr, "Asset '{}' is not ready: its load failed", slot.path);
    return slot.object.get();
}

} // namespace teserra::assets
