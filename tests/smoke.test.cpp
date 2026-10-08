#include <catch2/catch_test_macros.hpp>

TEST_CASE("test runner works", "[smoke]")
{
    REQUIRE(1 + 1 == 2);
}
