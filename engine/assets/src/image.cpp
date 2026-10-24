#include <teserra/assets/image.hpp>

#include <teserra/core/assert.hpp>

#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>

namespace {

// stb_image allocates through operator new, so its memory counts like the rest of the engine's, and tests
// can see that an oversized image is rejected before anything is allocated. The nothrow form, because stb
// expects nullptr when an allocation fails.
void* stbMalloc(std::size_t size) noexcept {
    return ::operator new(size, std::nothrow);
}

void stbFree(void* memory) noexcept {
    ::operator delete(memory);
}

void* stbRealloc(void* memory, std::size_t oldSize, std::size_t newSize) noexcept {
    void* resized = stbMalloc(newSize);
    if (resized != nullptr && memory != nullptr) {
        std::memcpy(resized, memory, std::min(oldSize, newSize));
        stbFree(memory);
    }
    return resized;
}

} // namespace

// The one translation unit that compiles stb_image (ADR-0010):
// - STB_IMAGE_STATIC keeps its functions local, so a game that compiles its own stb_image links too.
// - PNG only (docs/architecture/assets.md#image-decoding): every decoder left out is less code reading
//   untrusted bytes. JPEG or TGA would each be one define, plus a test.
// - Memory only: files are read through the platform layer, never by the decoder.
// The names are stb's configuration macros.
// NOLINTBEGIN(readability-identifier-naming)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#define STBI_ASSERT(condition) TESERRA_ASSERT(condition)
#define STBI_MALLOC(size) stbMalloc(size)
#define STBI_REALLOC_SIZED(memory, oldSize, newSize) stbRealloc(memory, oldSize, newSize)
#define STBI_FREE(memory) stbFree(memory)
// NOLINTEND(readability-identifier-naming)
#include <stb_image.h>

namespace teserra::assets {

namespace {

// The reason of stb's last failure on this thread.
std::string_view stbFailureReason() noexcept {
    const char* reason = stbi_failure_reason();
    return reason != nullptr ? std::string_view(reason) : std::string_view("unknown error");
}

struct StbImageDeleter {
    void operator()(stbi_uc* pixels) const noexcept { stbi_image_free(pixels); }
};

} // namespace

Result<ImageData> decodeImage(std::span<const std::byte> bytes, const ImageDecodeConfig& config) {
    if (bytes.empty()) {
        return fail(ErrorCode::InvalidData, Module::Assets, "Image data is empty");
    }
    // stb takes the length as an int.
    if (bytes.size() > static_cast<usize>(INT_MAX)) {
        return fail(ErrorCode::Unsupported, Module::Assets,
                    "Image data of {} bytes is over the decoder's limit of {} bytes", bytes.size(), INT_MAX);
    }
    const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
    const int length = static_cast<int>(bytes.size());

    // The header first: the limits are checked before the decoder allocates anything.
    int width = 0;
    int height = 0;
    int fileChannels = 0;
    if (stbi_info_from_memory(data, length, &width, &height, &fileChannels) == 0 || width <= 0 ||
        height <= 0) {
        return fail(ErrorCode::InvalidData, Module::Assets, "Cannot read the image header: {}",
                    stbFailureReason());
    }
    const auto imageWidth = static_cast<u32>(width);
    const auto imageHeight = static_cast<u32>(height);
    if (imageWidth > config.maxDimension || imageHeight > config.maxDimension) {
        return fail(ErrorCode::Unsupported, Module::Assets,
                    "Image of {}x{} pixels is over the limit of {} pixels per side", imageWidth, imageHeight,
                    config.maxDimension);
    }
    constexpr ImageFormat kFormat = ImageFormat::Rgba8;
    const u64 size = u64{imageWidth} * imageHeight * bytesPerPixel(kFormat);
    if (size > config.maxSizeInBytes) {
        return fail(ErrorCode::Unsupported, Module::Assets,
                    "Image of {}x{} pixels decodes to {} bytes, over the limit of {} bytes", imageWidth,
                    imageHeight, size, config.maxSizeInBytes);
    }

    int decodedWidth = 0;
    int decodedHeight = 0;
    const std::unique_ptr<stbi_uc, StbImageDeleter> pixels(
        stbi_load_from_memory(data, length, &decodedWidth, &decodedHeight, &fileChannels,
                              static_cast<int>(bytesPerPixel(kFormat))));
    if (pixels == nullptr) {
        const std::string_view reason = stbFailureReason();
        const ErrorCode code = reason == "Out of memory" ? ErrorCode::OutOfMemory : ErrorCode::InvalidData;
        return fail(code, Module::Assets, "Cannot decode the image: {}", reason);
    }
    // The same bytes went through the same header parser.
    TESERRA_ASSERT(decodedWidth == width && decodedHeight == height);

    const auto* first = reinterpret_cast<const std::byte*>(pixels.get());
    return ImageData{
        .width = imageWidth,
        .height = imageHeight,
        .format = kFormat,
        .pixels = std::vector<std::byte>(first, first + size),
    };
}

} // namespace teserra::assets
