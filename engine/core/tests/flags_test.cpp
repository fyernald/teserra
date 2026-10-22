#include <teserra/core/flags.hpp>

#include <teserra/core/types.hpp>

#include <catch2/catch_test_macros.hpp>

namespace flags_test {

enum class Access : teserra::u8 {
    None = 0,
    Read = 1U << 0U,
    Write = 1U << 1U,
    Execute = 1U << 2U,
};
TESERRA_FLAGS(Access)

} // namespace flags_test

using flags_test::Access;

// The operators are usable in constant expressions.
static_assert((Access::Read | Access::Write) == static_cast<Access>(3));
static_assert(hasAll(Access::Read | Access::Write, Access::Write));

TEST_CASE("TESERRA_FLAGS: combines and tests bits", "[core][flags]") {
    const Access readWrite = Access::Read | Access::Write;

    CHECK(hasAll(readWrite, Access::Read));
    CHECK(hasAll(readWrite, Access::Read | Access::Write));
    CHECK_FALSE(hasAll(readWrite, Access::Read | Access::Execute));
    CHECK(hasAny(readWrite, Access::Read | Access::Execute));
    CHECK_FALSE(hasAny(readWrite, Access::Execute));
    CHECK((readWrite & Access::Write) == Access::Write);
    CHECK((readWrite ^ Access::Read) == Access::Write);
}

TEST_CASE("TESERRA_FLAGS: the empty set and complements", "[core][flags]") {
    constexpr Access kNone = Access::None;

    CHECK(hasAll(Access::Read, kNone)); // every bit of the empty set is set
    CHECK_FALSE(hasAny(Access::Read, kNone));
    CHECK((~Access::Read & (Access::Read | Access::Write)) == Access::Write);
}

TEST_CASE("TESERRA_FLAGS: compound assignment", "[core][flags]") {
    Access access = Access::None;
    access |= Access::Read;
    access |= Access::Execute;
    CHECK(access == (Access::Read | Access::Execute));

    access &= ~Access::Read;
    CHECK(access == Access::Execute);

    access ^= Access::Execute;
    CHECK(access == Access::None);
    CHECK(Access{} == Access::None); // value-initialization is the empty set
}
