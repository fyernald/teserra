#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace teserra {

// Fixed-width aliases used throughout the engine (ADR-0021). Use usize for sizes and container
// indices, fixed-width types for data formats and GPU-visible structs.

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;

static_assert(sizeof(f32) == 4 && std::numeric_limits<f32>::is_iec559, "f32 must be IEEE-754 binary32");
static_assert(sizeof(f64) == 8 && std::numeric_limits<f64>::is_iec559, "f64 must be IEEE-754 binary64");

} // namespace teserra
