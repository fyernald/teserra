#include <teserra/core/version.hpp>

namespace teserra {

std::string_view versionString() noexcept {
    // Evaluated when the library is compiled, so it reports the library's version, not the caller's.
    return kVersionString;
}

} // namespace teserra
