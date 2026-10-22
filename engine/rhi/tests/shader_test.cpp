// The temporary GLSL blobs of the first triangle (removed by #44).

#include <teserra/rhi/shader.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::rhi;

TEST_CASE("temporaryGlslBlob: the source comes back unchanged", "[rhi][shader]") {
    constexpr std::string_view kSource = "#version 450 core\nvoid main() {}\n";

    const std::vector<std::byte> blob = temporaryGlslBlob(kSource);

    CHECK(blob.size() > kSource.size());
    const std::optional<std::string_view> source = temporaryGlslSource(blob);
    REQUIRE(source.has_value());
    const std::string_view text = source.value_or(std::string_view());
    CHECK(text == kSource);
    CHECK(text.data() == reinterpret_cast<const char*>(blob.data() + (blob.size() - kSource.size())));
    CHECK(temporaryGlslSource(temporaryGlslBlob("")) == std::string_view());
}

TEST_CASE("temporaryGlslSource: other blobs are not GLSL", "[rhi][shader]") {
    constexpr std::array<std::byte, 4> kShort{std::byte{'T'}, std::byte{'S'}, std::byte{'G'}, std::byte{'L'}};
    std::vector<std::byte> other = temporaryGlslBlob("void main() {}");
    other[0] = std::byte{'X'};

    CHECK_FALSE(temporaryGlslSource({}).has_value());
    CHECK_FALSE(temporaryGlslSource(kShort).has_value());
    CHECK_FALSE(temporaryGlslSource(other).has_value());
}
