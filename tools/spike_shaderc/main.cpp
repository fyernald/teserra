// SPIKE #36 (never merged): GLSL 450 (Vulkan dialect) -> SPIR-V with glslang -> reflection, (set, binding)
// remap and GL GLSL with SPIRV-Cross; then, on the GPU, the emitted GLSL and the ARB_gl_spirv route.
//
//   teserra_spike_shaderc <shader dir> [--gpu]

#include <teserra/platform/opengl_context.hpp>
#include <teserra/platform/platform.hpp>
#include <teserra/platform/window.hpp>

#include <glad/gl.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <spirv_glsl.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr unsigned kBindingsPerGroup = 8; // rhi.md: flat GL binding = set * 8 + binding

std::string readFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// #include "x" relative to the shader directory.
class Includer final : public glslang::TShader::Includer {
public:
    explicit Includer(fs::path directory)
        : m_directory(std::move(directory)) {}

    IncludeResult* includeLocal(const char* headerName, const char* /*includerName*/,
                                size_t /*inclusionDepth*/) override {
        const fs::path path = m_directory / headerName;
        if (!fs::exists(path)) {
            return nullptr;
        }
        auto* text = new std::string(readFile(path)); // NOLINT: freed in releaseInclude
        return new IncludeResult(path.generic_string(), text->data(), text->size(), text);
    }
    IncludeResult* includeSystem(const char* headerName, const char* includerName, size_t depth) override {
        return includeLocal(headerName, includerName, depth);
    }
    void releaseInclude(IncludeResult* result) override {
        if (result != nullptr) {
            delete static_cast<std::string*>(result->userData);
            delete result;
        }
    }

private:
    fs::path m_directory;
};

struct Compiled {
    bool ok = false;
    std::string log;
    std::vector<unsigned> spirv;
    double milliseconds = 0;
};

Compiled compile(const fs::path& path, EShLanguage stage, bool forOpenGl) {
    const auto start = std::chrono::steady_clock::now();
    Compiled result;
    const std::string source = readFile(path);
    const char* text = source.c_str();
    const std::string name = path.filename().string();
    const char* names[] = {name.c_str()};
    glslang::TShader shader(stage);
    shader.setStringsWithLengthsAndNames(&text, nullptr, names, 1);
    if (forOpenGl) {
        shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientOpenGL, 450);
        shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    } else {
        shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    }
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
    Includer includer(path.parent_path());
    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | (forOpenGl ? 0 : EShMsgVulkanRules));
    if (!shader.parse(GetDefaultResources(), 450, false, messages, includer)) {
        result.log = shader.getInfoLog();
        return result;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        result.log = program.getInfoLog();
        return result;
    }
    glslang::GlslangToSpv(*program.getIntermediate(stage), result.spirv);
    result.ok = true;
    result.milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}

std::string typeName(const spirv_cross::SPIRType& type) {
    static const std::map<spirv_cross::SPIRType::BaseType, std::string> kNames{
        {spirv_cross::SPIRType::Float, "float"}, {spirv_cross::SPIRType::Int, "int"},
        {spirv_cross::SPIRType::UInt, "uint"},   {spirv_cross::SPIRType::Struct, "struct"},
        {spirv_cross::SPIRType::SampledImage, "sampledImage"},
    };
    const auto found = kNames.find(type.basetype);
    std::string name = found != kNames.end() ? found->second : "other";
    if (type.columns > 1) {
        name += "mat" + std::to_string(type.columns) + "x" + std::to_string(type.vecsize);
    } else if (type.vecsize > 1) {
        name += std::to_string(type.vecsize);
    }
    return name;
}

// Reflects the resources and vertex inputs, remaps (set, binding) to flat GL bindings and emits GL GLSL.
std::string reflectAndEmit(const std::vector<unsigned>& spirv, bool vertex) {
    spirv_cross::CompilerGLSL glsl(spirv);
    const spirv_cross::ShaderResources resources = glsl.get_shader_resources();

    const auto remap = [&](const spirv_cross::Resource& resource, const char* kind) {
        const unsigned set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        const unsigned binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        const unsigned flat = set * kBindingsPerGroup + binding;
        std::string size;
        if (std::string(kind) == "uniform buffer") {
            size = ", " + std::to_string(glsl.get_declared_struct_size(glsl.get_type(resource.base_type_id))) +
                   " bytes";
        }
        std::printf("    %-15s %-10s set %u binding %u -> GL binding %u%s\n", kind, resource.name.c_str(), set,
                    binding, flat, size.c_str());
        glsl.set_decoration(resource.id, spv::DecorationBinding, flat);
        glsl.unset_decoration(resource.id, spv::DecorationDescriptorSet);
    };
    std::printf("  reflection:\n");
    for (const auto& ubo : resources.uniform_buffers) {
        remap(ubo, "uniform buffer");
    }
    for (const auto& image : resources.sampled_images) {
        remap(image, "sampled image");
    }
    if (vertex) {
        for (const auto& input : resources.stage_inputs) {
            std::printf("    vertex input    %-10s location %u, %s\n", input.name.c_str(),
                        glsl.get_decoration(input.id, spv::DecorationLocation),
                        typeName(glsl.get_type(input.type_id)).c_str());
        }
    }

    spirv_cross::CompilerGLSL::Options options;
    options.version = 450;
    options.es = false;
    options.vulkan_semantics = false;
    options.enable_420pack_extension = true; // keep layout(binding = N)
    options.separate_shader_objects = false;
    glsl.set_common_options(options);
    return glsl.compile();
}

// Rewrites the SPIR-V decorations for ARB_gl_spirv: flat bindings, DescriptorSet 0.
std::vector<unsigned> remapSpirv(std::vector<unsigned> spirv) {
    std::map<unsigned, unsigned> sets;
    for (size_t i = 5; i < spirv.size();) {
        const unsigned words = spirv[i] >> 16U;
        const unsigned opcode = spirv[i] & 0xFFFFU;
        if (opcode == spv::OpDecorate && spirv[i + 2] == spv::DecorationDescriptorSet) {
            sets[spirv[i + 1]] = spirv[i + 3];
        }
        i += words;
    }
    for (size_t i = 5; i < spirv.size();) {
        const unsigned words = spirv[i] >> 16U;
        const unsigned opcode = spirv[i] & 0xFFFFU;
        if (opcode == spv::OpDecorate && spirv[i + 2] == spv::DecorationBinding) {
            spirv[i + 3] += sets[spirv[i + 1]] * kBindingsPerGroup;
        } else if (opcode == spv::OpDecorate && spirv[i + 2] == spv::DecorationDescriptorSet) {
            spirv[i + 3] = 0;
        }
        i += words;
    }
    return spirv;
}

GLADapiproc loadFunction(void* context, const char* name) {
    return static_cast<teserra::platform::OpenGlContext*>(context)->getProcAddress(name);
}

void printBindings(const GladGLContext& gl, GLuint program) {
    for (const char* block : {"Frame", "Draw", "Material"}) {
        const GLuint index = gl.GetUniformBlockIndex(program, block);
        if (index == GL_INVALID_INDEX) {
            continue;
        }
        GLint binding = -1;
        gl.GetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_BINDING, &binding);
        std::printf("    block %-9s -> binding %d\n", block, binding);
    }
    GLint blocks = 0;
    gl.GetProgramInterfaceiv(program, GL_UNIFORM_BLOCK, GL_ACTIVE_RESOURCES, &blocks);
    for (GLint i = 0; i < blocks; ++i) {
        const GLenum props[] = {GL_BUFFER_BINDING, GL_BUFFER_DATA_SIZE};
        GLint values[2] = {};
        gl.GetProgramResourceiv(program, GL_UNIFORM_BLOCK, static_cast<GLuint>(i), 2, props, 2, nullptr, values);
        char name[64] = {};
        gl.GetProgramResourceName(program, GL_UNIFORM_BLOCK, static_cast<GLuint>(i), 64, nullptr, name);
        std::printf("    block #%d '%s' -> binding %d, %d bytes\n", i, name, values[0], values[1]);
    }
    GLint count = 0;
    gl.GetProgramInterfaceiv(program, GL_UNIFORM, GL_ACTIVE_RESOURCES, &count);
    for (GLint i = 0; i < count; ++i) {
        const GLenum props[] = {GL_TYPE, GL_LOCATION};
        GLint values[2] = {};
        gl.GetProgramResourceiv(program, GL_UNIFORM, static_cast<GLuint>(i), 2, props, 2, nullptr, values);
        if (values[0] == GL_SAMPLER_2D) {
            GLint unit = -1;
            gl.GetUniformiv(program, values[1], &unit);
            char name[64] = {};
            gl.GetProgramResourceName(program, GL_UNIFORM, static_cast<GLuint>(i), 64, nullptr, name);
            std::printf("    sampler %-8s -> unit %d\n", name, unit);
        }
    }
}

GLuint link(const GladGLContext& gl, GLuint vertex, GLuint fragment) {
    const GLuint program = gl.CreateProgram();
    gl.AttachShader(program, vertex);
    gl.AttachShader(program, fragment);
    gl.LinkProgram(program);
    GLint linked = 0;
    gl.GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == 0) {
        char log[2048] = {};
        gl.GetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::printf("    link failed: %s\n", log);
        return 0;
    }
    return program;
}

GLuint glslShader(const GladGLContext& gl, GLenum stage, const std::string& source) {
    const GLuint shader = gl.CreateShader(stage);
    const char* text = source.c_str();
    gl.ShaderSource(shader, 1, &text, nullptr);
    gl.CompileShader(shader);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == 0) {
        char log[2048] = {};
        gl.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::printf("    GLSL compile failed: %s\n", log);
    }
    return shader;
}

GLuint spirvShader(const GladGLContext& gl, GLenum stage, const std::vector<unsigned>& spirv) {
    const GLuint shader = gl.CreateShader(stage);
    gl.ShaderBinary(1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V_ARB, spirv.data(),
                    static_cast<GLsizei>(spirv.size() * sizeof(unsigned)));
    gl.SpecializeShaderARB(shader, "main", 0, nullptr, nullptr);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == 0) {
        char log[2048] = {};
        gl.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::printf("    SPIR-V specialization failed: %s\n", log);
    }
    return shader;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: teserra_spike_shaderc <shader dir> [--gpu]\n");
        return 2;
    }
    const fs::path directory = argv[1];
    const bool gpu = argc > 2 && std::string(argv[2]) == "--gpu";
    glslang::InitializeProcess();

    std::map<std::string, std::vector<unsigned>> vulkanSpirv;
    std::map<std::string, std::string> emitted;
    for (const auto& [file, stage] : {std::pair{"sprite.vert", EShLangVertex}, std::pair{"sprite.frag", EShLangFragment},
                                      std::pair{"broken.frag", EShLangFragment},
                                      std::pair{"include_error.frag", EShLangFragment}}) {
        std::printf("== %s\n", file);
        const Compiled compiled = compile(directory / file, stage, /*forOpenGl=*/false);
        if (!compiled.ok) {
            std::printf("  glslang failed:\n%s", compiled.log.c_str());
            continue;
        }
        std::printf("  SPIR-V: %zu bytes in %.2f ms (cold process: includes glslang setup for the first)\n",
                    compiled.spirv.size() * 4, compiled.milliseconds);
        std::ofstream(directory / (std::string(file) + ".spv"), std::ios::binary)
            .write(reinterpret_cast<const char*>(compiled.spirv.data()),
                   static_cast<std::streamsize>(compiled.spirv.size() * 4));
        const auto start = std::chrono::steady_clock::now();
        emitted[file] = reflectAndEmit(compiled.spirv, stage == EShLangVertex);
        std::printf("  SPIRV-Cross reflect + emit: %.2f ms\n  emitted GL GLSL:\n%s\n",
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
                    emitted[file].c_str());
        vulkanSpirv[file] = compiled.spirv;

        const Compiled forGl = compile(directory / file, stage, /*forOpenGl=*/true);
        std::printf("  glslang with the OpenGL client (for ARB_gl_spirv): %s\n%s", forGl.ok ? "ok" : "failed",
                    forGl.log.c_str());
    }
    glslang::FinalizeProcess();
    if (!gpu) {
        return 0;
    }

    std::printf("== GPU\n");
    auto platform = teserra::platform::Platform::create();
    if (!platform) {
        std::printf("no platform\n");
        return 1;
    }
    auto window = (*platform)->createWindow({.title = "spike",
                                             .size = {64, 64},
                                             .visible = false,
                                             .graphicsApi = teserra::platform::GraphicsApi::OpenGL,
                                             .debugContext = true});
    if (!window) {
        std::printf("no window\n");
        return 1;
    }
    teserra::platform::OpenGlContext& context = *(*window)->openGlContext();
    context.makeCurrent();
    GladGLContext gl{};
    gladLoadGLContextUserPtr(&gl, &loadFunction, &context);
    std::printf("  %s, %s; ARB_gl_spirv: %s\n", reinterpret_cast<const char*>(gl.GetString(GL_RENDERER)),
                reinterpret_cast<const char*>(gl.GetString(GL_VERSION)), gl.ARB_gl_spirv != 0 ? "yes" : "no");

    std::printf("  emitted GLSL path:\n");
    const GLuint glslProgram = link(gl, glslShader(gl, GL_VERTEX_SHADER, emitted["sprite.vert"]),
                                    glslShader(gl, GL_FRAGMENT_SHADER, emitted["sprite.frag"]));
    if (glslProgram != 0) {
        printBindings(gl, glslProgram);
    }

    if (gl.ARB_gl_spirv != 0) {
        for (const bool remapped : {false, true}) {
            std::printf("  ARB_gl_spirv path, Vulkan SPIR-V %s:\n",
                        remapped ? "with flat bindings and set 0" : "as compiled (sets 0, 2, 3)");
            const auto prepare = [&](const std::string& file) {
                return remapped ? remapSpirv(vulkanSpirv[file]) : vulkanSpirv[file];
            };
            const GLuint program = link(gl, spirvShader(gl, GL_VERTEX_SHADER, prepare("sprite.vert")),
                                        spirvShader(gl, GL_FRAGMENT_SHADER, prepare("sprite.frag")));
            if (program != 0) {
                printBindings(gl, program);
            }
        }
    }
    return 0;
}
