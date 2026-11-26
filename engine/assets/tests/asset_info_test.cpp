// AssetRegistry::forEach() (#301): every asset with its path, its type's name from the loader, its state
// (loaded, fallback, failed) and its handles; assets released at the end of a frame are gone after it.

#include <teserra/assets/asset_registry.hpp>
#include <teserra/core/result.hpp>
#include <teserra/test/log_capture.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::assets;

namespace {

struct Note {
    std::string text;
};
struct Sound {
    int samples = 0;
};

class NoteLoader final : public AssetLoader<Note> {
public:
    [[nodiscard]] std::string_view typeName() const override { return "Note"; }
    [[nodiscard]] std::span<const std::string_view> extensions() const override { return kExtensions; }
    [[nodiscard]] Result<Note> load(AssetLoadContext& /*context*/,
                                    std::span<const std::byte> /*bytes*/) override {
        return Note{};
    }

private:
    static constexpr std::array<std::string_view, 1> kExtensions{"note"};
};

struct Seen {
    std::string path;
    std::string typeName;
    AssetState state;
    u32 references;

    friend bool operator==(const Seen&, const Seen&) = default;
};

std::vector<Seen> visit(const AssetRegistry& registry) {
    std::vector<Seen> seen;
    registry.forEach([&](const AssetInfo& info) {
        CHECK(info.id == AssetId::fromPath(info.path));
        seen.push_back({std::string(info.path), std::string(info.typeName), info.state, info.references});
    });
    std::ranges::sort(seen, {}, &Seen::path);
    return seen;
}

Result<Note> missingNote() {
    return fail(ErrorCode::NotFound, Module::Game, "No such note");
}

} // namespace

TEST_CASE("AssetRegistry::forEach reports every asset's path, type name, state and handles", "[assets]") {
    const test::LogCapture logs; // the failed loads log errors
    AssetRegistry registry;
    registry.registerLoader<Note>(std::make_unique<NoteLoader>());

    Asset<Note> first = registry.load<Note>("game://a.note", [] { return Result<Note>(Note{"a"}); });
    const Asset<Note> again = registry.load<Note>("game://a.note", [] { return Result<Note>(Note{"a"}); });
    Asset<Note> failed = registry.load<Note>("game://missing.note", missingNote);
    const Asset<Sound> sound =
        registry.load<Sound>("game://boom.wav", [] { return Result<Sound>(Sound{.samples = 4}); });

    CHECK(visit(registry) == std::vector<Seen>{
                                 {"game://a.note", "Note", AssetState::Loaded, 2},
                                 {"game://boom.wav", "", AssetState::Loaded, 1}, // no loader names Sound
                                 {"game://missing.note", "Note", AssetState::Failed, 1},
                             });

    // A fallback binds the failed slot that exists.
    registry.setFallback(Note{"placeholder"});
    CHECK(visit(registry)[2].state == AssetState::Fallback);

    // Released: listed with no handles until the end of the frame unloads it.
    failed = {};
    CHECK(visit(registry)[2] == Seen{"game://missing.note", "Note", AssetState::Fallback, 0});
    registry.endFrame();
    CHECK(visit(registry).size() == 2);

    first = {};
    registry.endFrame();
    CHECK(visit(registry)[0].references == 1); // `again` keeps it
}
