#include <teserra/assets/asset_registry.hpp>

#include <teserra/core/log.hpp>

#include <algorithm>
#include <utility>

namespace teserra::assets {

namespace {

// The extension of the last component of `path`, without the dot: "png" for "assets://a.b/hero.png"; empty
// when the name has no dot.
[[nodiscard]] std::string_view fileExtension(std::string_view path) noexcept {
    const usize nameStart = path.find_last_of("/\\");
    const std::string_view name = nameStart == std::string_view::npos ? path : path.substr(nameStart + 1);
    const usize dot = name.rfind('.');
    return dot == std::string_view::npos ? std::string_view{} : name.substr(dot + 1);
}

[[nodiscard]] bool equalsIgnoringCase(std::string_view lowercase, std::string_view text) noexcept {
    return std::ranges::equal(lowercase, text, [](char left, char right) {
        return left == (right >= 'A' && right <= 'Z' ? static_cast<char>(right - 'A' + 'a') : right);
    });
}

} // namespace

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
            // Only when the ID maps to this slot: with assertions off, a dependency cycle leaves a second
            // slot for one ID.
            if (const auto mapped = m_byId.find(slot.id);
                mapped != m_byId.end() && mapped->second == handle) {
                m_byId.erase(mapped);
            }
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

void AssetRegistry::addLoader(detail::AssetTypeId type, std::span<const std::string_view> extensions,
                              std::unique_ptr<detail::AssetLoaderBase> loader) {
    for (const std::string_view extension : extensions) {
        TESERRA_ASSERT(!extension.empty() && std::ranges::none_of(extension,
                                                                  [](char character) {
                                                                      return character == '.' ||
                                                                             (character >= 'A' &&
                                                                              character <= 'Z');
                                                                  }),
                       "Loader extension '{}' must be lowercase, without the dot", extension);
        TESERRA_ASSERT(std::ranges::none_of(m_loaders,
                                            [&](const LoaderEntry& entry) {
                                                return entry.type == type &&
                                                       std::ranges::find(entry.extensions, extension) !=
                                                           entry.extensions.end();
                                            }),
                       "Two loaders of one asset type read '.{}' files", extension);
    }
    m_loaders.push_back({.type = type, .extensions = extensions, .loader = std::move(loader)});
}

Result<detail::AssetLoaderBase*> AssetRegistry::findLoader(detail::AssetTypeId type,
                                                           std::string_view virtualPath) const {
    const std::string_view extension = fileExtension(virtualPath);
    for (const LoaderEntry& entry : m_loaders) {
        if (entry.type == type && std::ranges::any_of(entry.extensions, [&](std::string_view candidate) {
                return equalsIgnoringCase(candidate, extension);
            })) {
            return entry.loader.get();
        }
    }
    if (extension.empty()) {
        return fail(ErrorCode::Unsupported, Module::Assets,
                    "The file has no extension, so no loader can be chosen for it");
    }
    return fail(ErrorCode::Unsupported, Module::Assets, "No loader of this asset type reads '.{}' files",
                extension);
}

Result<std::vector<std::byte>> AssetRegistry::readFile(std::string_view virtualPath) const {
    if (m_config.mounts == nullptr) {
        return fail(ErrorCode::Unsupported, Module::Assets,
                    "The asset registry has no mount table to read '{}' with", virtualPath);
    }
    return m_config.mounts->readFile(virtualPath);
}

Result<std::vector<std::byte>> AssetLoadContext::readFile(std::string_view virtualPath) const {
    return m_registry->readFile(virtualPath);
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
