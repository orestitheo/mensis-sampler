#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <limits>
#include <vector>
#include "core/RenderStats.h"

using Catch::Matchers::WithinAbs;

TEST_CASE("measure reports peak and DC offset") {
    std::vector<float> x{0.5f, -1.0f, 0.5f, 0.0f};
    auto s = demea::measure(x);
    CHECK(s.finite);
    CHECK(s.peak == 1.0f);
    CHECK_THAT(s.dcOffset, WithinAbs(0.0, 1e-9));
}

TEST_CASE("measure flags NaN and Inf") {
    std::vector<float> x{0.1f, std::numeric_limits<float>::quiet_NaN()};
    CHECK_FALSE(demea::measure(x).finite);
    x[1] = std::numeric_limits<float>::infinity();
    CHECK_FALSE(demea::measure(x).finite);
}

TEST_CASE("measure handles empty input") {
    auto s = demea::measure({});
    CHECK(s.finite);
    CHECK(s.peak == 0.0f);
}
