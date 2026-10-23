#include "spirv_validation.hpp"

#if TESERRA_HAS_SPIRV_TOOLS
#include <spirv-tools/libspirv.hpp>
#endif

namespace teserra::shaderc::test {

bool spirvValidationAvailable() noexcept {
    return TESERRA_HAS_SPIRV_TOOLS != 0;
}

#if TESERRA_HAS_SPIRV_TOOLS

std::string validateSpirv(std::span<const std::uint32_t> spirv) {
    spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_2);
    std::string messages;
    tools.SetMessageConsumer(
        [&messages](spv_message_level_t, const char*, const spv_position_t& position, const char* message) {
            messages += "word " + std::to_string(position.index) + ": " + message + '\n';
        });
    if (!tools.Validate(spirv.data(), spirv.size()) && messages.empty()) {
        messages = "invalid SPIR-V";
    }
    return messages;
}

#else

std::string validateSpirv(std::span<const std::uint32_t> /*spirv*/) {
    return "SPIR-V validation needs TESERRA_SHADER_VALIDATION";
}

#endif

} // namespace teserra::shaderc::test
