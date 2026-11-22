// The upload ring on the null backend: one partition per frame slot, aligned sub-allocations into mapped
// memory, and assertions when a frame would write into memory that another frame's GPU work may still read.

#include <teserra/renderer/upload_ring.hpp>

#include <teserra/rhi_null/null_device.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <utility>

using namespace teserra;
using namespace teserra::renderer;

TEST_CASE("UploadRing: one partition per frame slot, rounded up to 256 bytes", "[renderer][upload]") {
    const auto device = rhi::null::createHeadlessDevice();
    Result<UploadRing> created = UploadRing::create(*device, 1000);
    REQUIRE(created.has_value());
    UploadRing& ring = *created;

    CHECK(ring.bytesPerFrame() == 1024);
    const rhi::BufferDesc* buffer = device->buffer(ring.buffer());
    REQUIRE(buffer != nullptr);
    CHECK(buffer->sizeInBytes == 2048);
    CHECK(buffer->memory == rhi::MemoryLocation::Upload);
    CHECK(hasAll(buffer->usage,
                 rhi::BufferUsage::Uniform | rhi::BufferUsage::Vertex | rhi::BufferUsage::Index));

    const auto frame = [&](auto&& body) {
        device->beginFrame();
        ring.beginFrame();
        body();
        device->endFrame();
    };
    frame([&] { // slot 0
        const std::optional<UploadRing::Allocation> first = ring.allocate(100, 16);
        const std::optional<UploadRing::Allocation> second = ring.allocate(8, 256);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK(first->offset == 0);
        CHECK(first->memory.size() == 100);
        CHECK(second->offset == 256); // aligned up past the first allocation
        CHECK(ring.usedBytes() == 264);

        // The memory is the buffer's mapping.
        second->memory[0] = std::byte{0x5A};
        CHECK(device->bufferData(ring.buffer())[256] == std::byte{0x5A});
    });
    frame([&] { // slot 1: the second partition, empty again
        CHECK(ring.usedBytes() == 0);
        CHECK(ring.allocate(4, 4)->offset == 1024);
    });
    frame([&] { CHECK(ring.allocate(4, 4)->offset == 0); }); // slot 0 again, after its frame completed
}

TEST_CASE("UploadRing: creation checks the size", "[renderer][upload]") {
    const auto device = rhi::null::createHeadlessDevice();

    const Result<UploadRing> empty = UploadRing::create(*device, 0);
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().code == ErrorCode::InvalidArgument);
    CHECK_FALSE(UploadRing::create(*device, u64{3} * 1024 * 1024 * 1024).has_value()); // 6 GiB in all
    CHECK(device->liveObjectCount() == 0);
}

TEST_CASE("UploadRing: a moved ring owns the buffer", "[renderer][upload]") {
    const auto device = rhi::null::createHeadlessDevice();
    {
        UploadRing ring = *UploadRing::create(*device, 256);
        const rhi::BufferHandle buffer = ring.buffer();
        UploadRing moved = std::move(ring);
        CHECK(moved.buffer() == buffer);
        CHECK(device->liveObjectCount() == 1);
    }
    CHECK(device->liveObjectCount() == 0);
}

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("UploadRing: writes that could reach data in flight assert", "[renderer][upload][validation]") {
    const auto device = rhi::null::createHeadlessDevice();
    UploadRing ring = *UploadRing::create(*device, 256);
    const auto failureOf = [](auto&& call) { return test::requireAssert(call).message(); };

    SECTION("before the first frame") {
        CHECK(failureOf([&] { static_cast<void>(ring.allocate(4, 4)); }) ==
              "UploadRing::allocate before the first beginFrame()");
    }
    SECTION("a full partition") {
        device->beginFrame();
        ring.beginFrame();
        CHECK(ring.allocate(200, 4).has_value());
        CHECK(failureOf([&] { static_cast<void>(ring.allocate(100, 4)); }) ==
              "UploadRing::allocate of 100 bytes: the frame's 256 bytes are full (200 used)");
        device->endFrame();
    }
    SECTION("a new device frame without beginFrame()") {
        device->beginFrame();
        ring.beginFrame();
        device->endFrame();
        device->beginFrame(); // slot 1, but the ring still writes slot 0, which frame 0 may be reading
        CHECK(failureOf([&] { static_cast<void>(ring.allocate(4, 4)); }) ==
              "UploadRing::allocate in frame slot 1, but beginFrame() was called for slot 0: the slot's data "
              "could still be in flight");
        device->endFrame();
    }
}

#endif
