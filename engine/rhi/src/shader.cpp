#include <teserra/rhi/shader.hpp>

#include <algorithm>
#include <cstring>

namespace teserra::rhi {

namespace {

// TEMPORARY (#44): marks a blob of temporaryGlslBlob(); the source follows unchanged.
constexpr std::string_view kTemporaryGlslMagic = "TSGLSL01";

} // namespace

std::vector<std::byte> temporaryGlslBlob(std::string_view source) {
    std::vector<std::byte> blob(kTemporaryGlslMagic.size() + source.size());
    std::memcpy(blob.data(), kTemporaryGlslMagic.data(), kTemporaryGlslMagic.size());
    if (!source.empty()) {
        std::memcpy(blob.data() + kTemporaryGlslMagic.size(), source.data(), source.size());
    }
    return blob;
}

std::optional<std::string_view> temporaryGlslSource(std::span<const std::byte> blob) noexcept {
    if (blob.size() < kTemporaryGlslMagic.size() ||
        !std::ranges::equal(blob.first(kTemporaryGlslMagic.size()), kTemporaryGlslMagic,
                            [](std::byte byte, char c) { return byte == static_cast<std::byte>(c); })) {
        return std::nullopt;
    }
    const std::span<const std::byte> source = blob.subspan(kTemporaryGlslMagic.size());
    return std::string_view(reinterpret_cast<const char*>(source.data()), source.size());
}

} // namespace teserra::rhi
