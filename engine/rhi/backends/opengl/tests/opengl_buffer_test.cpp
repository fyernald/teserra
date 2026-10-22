// OpenGL buffers against the fake driver of fake_gl.hpp, which keeps buffer contents, so uploads, copies and
// readback run as a round trip in CI. opengl_gpu_test.cpp repeats the round trip on a real driver.

#include <teserra/rhi/device.hpp>
#include <teserra/rhi_opengl/opengl_backend.hpp>
#include <teserra/test/assert.hpp>

#include "fake_gl.hpp"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <vector>

using namespace teserra;
using namespace teserra::rhi;

namespace {

std::unique_ptr<Device> requireOpenGl(test::FakeGlWindow& window) {
    Result<std::unique_ptr<Device>> device =
        createDevice({.backend = Backend::OpenGL, .enableValidation = true, .enableBackendDebug = false},
                     &window, std::array{opengl::backendFactory()});
    if (!device) {
        FAIL(std::format("{}", device.error()));
    }
    return std::move(device).value();
}

std::vector<std::byte> bytes(std::initializer_list<u8> values) {
    std::vector<std::byte> result;
    for (const u8 value : values) {
        result.push_back(std::byte{value});
    }
    return result;
}

bool called(const test::FakeGlState& gl, std::string_view call) {
    return std::ranges::find(gl.calls, call) != gl.calls.end();
}

} // namespace

TEST_CASE("OpenGL backend: buffers get immutable storage for their memory location", "[rhi_opengl]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    const GLuint first = gl.lastName + 1; // GL names of the three buffers: first, first + 1, first + 2
    gl.calls.clear();

    const BufferHandle vertices = *device->createBuffer(
        {.sizeInBytes = 64, .usage = BufferUsage::Vertex | BufferUsage::CopyDst, .debugName = "Vertices"});
    const BufferHandle frameData = *device->createBuffer(
        {.sizeInBytes = 256, .usage = BufferUsage::Uniform, .memory = MemoryLocation::Upload});
    const BufferHandle readback = *device->createBuffer(
        {.sizeInBytes = 16, .usage = BufferUsage::CopyDst, .memory = MemoryLocation::Readback});

    const std::vector<std::string> expected{
        std::format("CreateBuffers() = {}", first),
        std::format("NamedBufferStorage({}, 64, GL_DYNAMIC_STORAGE_BIT)", first),
        std::format("ObjectLabel(GL_BUFFER, {}, Vertices)", first),
        std::format("CreateBuffers() = {}", first + 1),
        std::format("NamedBufferStorage({}, 256, GL_MAP_WRITE_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT)",
                    first + 1),
        std::format(
            "MapNamedBufferRange({}, 0, 256, GL_MAP_WRITE_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT)",
            first + 1),
        std::format("CreateBuffers() = {}", first + 2),
        std::format("NamedBufferStorage({}, 16, GL_MAP_READ_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT|"
                    "GL_CLIENT_STORAGE_BIT)",
                    first + 2),
        std::format(
            "MapNamedBufferRange({}, 0, 16, GL_MAP_READ_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT)",
            first + 2),
    };
    CHECK(gl.calls == expected);

    // Mapped once, for the buffer's lifetime.
    const std::span<std::byte> mapped = device->map(frameData);
    CHECK(mapped.size() == 256);
    CHECK(mapped.data() == gl.buffers.at(first + 1).data());
    CHECK(device->map(frameData).data() == mapped.data());
    CHECK(device->map(readback).size() == 16);

    device->destroy(vertices);
    device->destroy(frameData);
    device->destroy(BufferHandle{}); // null handles are ignored
    CHECK(called(gl, std::format("DeleteBuffers({})", first)));
    CHECK(called(gl, std::format("DeleteBuffers({})", first + 1)));
    CHECK(gl.buffers.size() == 1);
}

TEST_CASE("OpenGL backend: the device deletes the buffers left at its end", "[rhi_opengl]") {
    test::FakeGlWindow window;
    {
        const auto device = requireOpenGl(window);
        static_cast<void>(*device->createBuffer({.sizeInBytes = 4, .usage = BufferUsage::Index}));
        CHECK(window.context.state.buffers.size() == 1);
    }
    CHECK(window.context.state.buffers.empty());
}

TEST_CASE("OpenGL backend: writeBuffer, copyBuffer and map() round trip", "[rhi_opengl]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    const BufferHandle source = *device->createBuffer(
        {.sizeInBytes = 8, .usage = BufferUsage::CopySrc | BufferUsage::CopyDst, .debugName = "Source"});
    const BufferHandle upload = *device->createBuffer(
        {.sizeInBytes = 4, .usage = BufferUsage::CopySrc, .memory = MemoryLocation::Upload});
    const BufferHandle readback = *device->createBuffer(
        {.sizeInBytes = 8, .usage = BufferUsage::CopyDst, .memory = MemoryLocation::Readback});
    const GLuint readbackName = gl.lastName;
    gl.calls.clear();

    device->writeBuffer(source, 2, bytes({1, 2, 3, 4}));
    device->writeBuffer(source, 8, {}); // empty data reaches no GL call
    std::ranges::copy(bytes({9, 8}), device->map(upload).begin());

    device->beginFrame();
    CommandList& cmd = device->acquireCommandList();
    cmd.copyBuffer({.source = source, .destination = readback, .size = 8});
    cmd.copyBuffer({.source = upload, .destination = readback, .destinationOffset = 6, .size = 2});
    device->submit(cmd);
    device->endFrame();
    device->waitIdle();

    const std::vector<std::string> expected{
        std::format("NamedBufferSubData({}, 2, 4)", readbackName - 2),
        std::format("CopyNamedBufferSubData({} -> {}, 0 -> 0, 8)", readbackName - 2, readbackName),
        std::format("CopyNamedBufferSubData({} -> {}, 0 -> 6, 2)", readbackName - 1, readbackName),
    };
    CHECK(gl.calls == expected);
    CHECK(std::ranges::equal(device->map(readback), bytes({0, 0, 1, 2, 3, 4, 9, 8})));
}

TEST_CASE("OpenGL backend: buffer creation failures are errors", "[rhi_opengl]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;

    SECTION("out of memory") {
        gl.nextError = GL_OUT_OF_MEMORY; // the fake still allocates, so the size stays small
        const auto buffer =
            device->createBuffer({.sizeInBytes = 4096, .usage = BufferUsage::Vertex, .debugName = "Huge"});
        REQUIRE_FALSE(buffer.has_value());
        CHECK(buffer.error().code == ErrorCode::OutOfMemory);
        CHECK(buffer.error().message == "Out of GPU memory for buffer 'Huge' (4096 bytes)");
    }
    SECTION("mapping") {
        gl.failMapping = true;
        const auto buffer = device->createBuffer({.sizeInBytes = 64,
                                                  .usage = BufferUsage::Uniform,
                                                  .memory = MemoryLocation::Upload,
                                                  .debugName = "Frame data"});
        REQUIRE_FALSE(buffer.has_value());
        CHECK(buffer.error().code == ErrorCode::DeviceFailure);
        CHECK(buffer.error().message == "Could not map buffer 'Frame data' (64 bytes)");
    }
    // The GL buffer is deleted again.
    CHECK(gl.buffers.empty());
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("OpenGL backend: buffer misuse asserts", "[rhi_opengl][validation]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    const BufferHandle vertices = *device->createBuffer(
        {.sizeInBytes = 8, .usage = BufferUsage::Vertex | BufferUsage::CopyDst, .debugName = "Vertices"});
    const BufferHandle stale = *device->createBuffer({.sizeInBytes = 8, .usage = BufferUsage::CopySrc});
    device->destroy(stale);
    const auto failureOf = [](auto&& call) { return test::requireAssert(call).message(); };

    // The shared checks of <teserra/rhi/validation.hpp>, which null-backend tests cover in full.
    CHECK(failureOf([&] { device->writeBuffer(vertices, 6, bytes({1, 2, 3, 4})); }) ==
          "writeBuffer of 4 bytes at offset 6 is outside buffer 'Vertices' (8 bytes)");
    CHECK(failureOf([&] { static_cast<void>(device->map(vertices)); }) ==
          "map() of buffer 'Vertices', which is GpuOnly; only Upload and Readback buffers are mapped");

    // Liveness, which the device checks itself.
    CHECK(failureOf([&] { device->writeBuffer(stale, 0, bytes({1})); }) ==
          std::format("writeBuffer to buffer {}, which is not alive", stale));
    CHECK(failureOf([&] { static_cast<void>(device->map(stale)); }) ==
          std::format("map() of buffer {}, which is not alive", stale));
    CHECK(failureOf([&] { device->destroy(stale); }) ==
          std::format("Destroying buffer {}, which is not alive", stale));

    device->beginFrame();
    CommandList& cmd = device->acquireCommandList();
    cmd.copyBuffer({.source = stale, .destination = vertices, .size = 4});
    CHECK(failureOf([&] { device->submit(cmd); }) ==
          std::format("copyBuffer reads buffer {}, which is not alive", stale));
    device->endFrame();
}
#endif
