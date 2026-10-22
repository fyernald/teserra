// Compile-fail fixture (core.result.discarding_is_diagnosed): ignoring a returned Result is diagnosed
// even when the function itself lacks [[nodiscard]], because the Result type is [[nodiscard]] (ADR-0016).
// Built with warnings as errors.

#include <teserra/core/result.hpp>

teserra::Result<int> loadWithoutNodiscard() {
    return 1;
}

void ignoreTheResult() {
    loadWithoutNodiscard();
}
