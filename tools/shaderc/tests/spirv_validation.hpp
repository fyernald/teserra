#pragma once

// SPIR-V validation for the tests: SPIRV-Tools' validator, the checks of spirv-val, in the Vulkan 1.2
// environment. Available when the build has TESERRA_SHADER_VALIDATION (ADR-0017).

#include <cstdint>
#include <span>
#include <string>

namespace teserra::shaderc::test {

[[nodiscard]] bool spirvValidationAvailable() noexcept;

/// The validator's messages, empty when `spirv` is valid. Only call when spirvValidationAvailable().
[[nodiscard]] std::string validateSpirv(std::span<const std::uint32_t> spirv);

} // namespace teserra::shaderc::test
