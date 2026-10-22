// Core sandbox: the Iteration 2 demo (#9). Uses the core library the way a game would: logging in several
// categories, a recoverable error handled with a fallback, generational handles, math, and an assertion.
//
//   teserra_core_sandbox            runs everything except the assertion and exits with 0
//   teserra_core_sandbox --assert   also fails a TESERRA_ASSERT: in Debug the failure is logged with file
//                                   and line and the program aborts (breaks into an attached debugger)

#include <teserra/core/assert.hpp>
#include <teserra/core/handle.hpp>
#include <teserra/core/handle_pool.hpp>
#include <teserra/core/log.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/profile.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/version.hpp>

#include <charconv>
#include <string>
#include <string_view>
#include <system_error>

using namespace teserra;

namespace {

// A game's own log category, next to the engine's. Registered on first use rather than during static
// initialization, where a failure could not be handled.
LogCategory sandboxCategory() {
    static const LogCategory kCategory = log::registerCategory("Sandbox");
    return kCategory;
}

using TextureHandle = Handle<struct TextureTag>;

struct Texture {
    std::string name;
    UVec2 size;
};

Result<u32> parseDimension(std::string_view text) {
    u32 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0) {
        return fail(ErrorCode::InvalidData, Module::Game, "'{}' is not a positive number", text);
    }
    return value;
}

// Parses "<width>x<height>", e.g. "64x64".
Result<UVec2> parseSize(std::string_view text) {
    const usize separator = text.find('x');
    if (separator == std::string_view::npos) {
        return fail(ErrorCode::InvalidData, Module::Game, "Size '{}' is not <width>x<height>", text);
    }
    TESERRA_TRY_ASSIGN(const u32 width, parseDimension(text.substr(0, separator)));
    TESERRA_TRY_ASSIGN(const u32 height, parseDimension(text.substr(separator + 1)));
    return UVec2(width, height);
}

// Stands in for an asset loader: fails for bad metadata, and the caller decides what to do.
Result<TextureHandle> loadTexture(HandlePool<Texture, TextureTag>& textures, std::string_view name,
                                  std::string_view size) {
    TESERRA_TRY_ASSIGN(const UVec2 parsed, parseSize(size));
    TESERRA_LOG_DEBUG(LogCategory::Assets, "Loaded texture '{}' ({}x{})", name, parsed.x, parsed.y);
    return textures.create(Texture{std::string(name), parsed});
}

TextureHandle loadOrFallback(HandlePool<Texture, TextureTag>& textures, TextureHandle fallback,
                             std::string_view name, std::string_view size) {
    const Result<TextureHandle> texture = loadTexture(textures, name, size);
    if (!texture) {
        // The error is logged once, here, where it is handled (ADR-0016).
        TESERRA_LOG_WARN(sandboxCategory(), "Using the fallback texture for '{}': {}", name, texture.error());
        return fallback;
    }
    return *texture;
}

void demonstrateHandles() {
    HandlePool<Texture, TextureTag> textures;
    const TextureHandle fallback = textures.create(Texture{"fallback", UVec2(2, 2)});

    const TextureHandle hero = loadOrFallback(textures, fallback, "hero", "64x64");
    const TextureHandle tiles = loadOrFallback(textures, fallback, "tiles", "64 by 64");
    TESERRA_LOG_INFO(sandboxCategory(), "hero -> handle {} ('{}'), tiles -> handle {} ('{}')", hero,
                     textures.get(hero).name, tiles, textures.get(tiles).name);

    textures.destroy(hero);
    const TextureHandle reused = textures.create(Texture{"enemy", UVec2(32, 32)});
    TESERRA_LOG_INFO(sandboxCategory(),
                     "After destroying hero: handle {} is {}; its slot now holds handle {} ('{}')", hero,
                     textures.contains(hero) ? "live" : "stale", reused, textures.get(reused).name);
}

void demonstrateMath() {
    // A 2D camera: 1280x720 world units, +Y up, looking down -Z (ADR-0024).
    const Mat4 projection = math::orthographic(0.0F, 1280.0F, 0.0F, 720.0F, -1.0F, 1.0F);
    const Vec4 topRight = projection * Vec4(1280.0F, 720.0F, 0.0F, 1.0F);
    const Aabb player{{100.0F, 100.0F, 0.0F}, {132.0F, 164.0F, 0.0F}};
    const Aabb coin{{120.0F, 150.0F, 0.0F}, {136.0F, 166.0F, 0.0F}};
    TESERRA_LOG_INFO(LogCategory::Scene, "Top-right corner maps to NDC ({}, {}); player touches coin: {}",
                     topRight.x, topRight.y, math::intersects(player, coin));
}

void spawnSprite(u32 index, u32 spriteCount) {
    TESERRA_ASSERT(index < spriteCount, "Sprite index {} out of range ({} sprites)", index, spriteCount);
    TESERRA_LOG_INFO(sandboxCategory(), "Spawned sprite {}", index);
}

} // namespace

// Only std::bad_alloc can escape, and terminating is the right response to it here.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char** argv) {
    TESERRA_PROFILE_FUNCTION();
    const bool triggerAssert = argc > 1 && std::string_view(argv[1]) == "--assert";

    TESERRA_LOG_INFO(LogCategory::App, "Teserra {} core sandbox", versionString());
    TESERRA_LOG_DEBUG(LogCategory::Core, "Debug messages are shown in Debug builds only by default");
    TESERRA_LOG_TRACE(LogCategory::Core, "Trace messages are compiled in for Debug, but filtered at runtime");

    demonstrateHandles();
    demonstrateMath();

    spawnSprite(3, 8);
    if (!triggerAssert) {
        TESERRA_LOG_INFO(LogCategory::App, "Run with --assert to see a failed TESERRA_ASSERT");
        return 0;
    }
#if TESERRA_ENABLE_ASSERTS
    spawnSprite(12, 8); // fails: logged at Critical with this file and line, then aborts
#else
    TESERRA_LOG_INFO(LogCategory::App, "TESERRA_ASSERT is compiled out in this build; nothing to show");
#endif
    return 0;
}
