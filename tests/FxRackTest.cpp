#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include "core/RenderStats.h"
#include "core/engine/FxRack.h"

namespace {

constexpr double kRate = 48000.0;

// 4 s of a 220 Hz sine through the rack, in host-sized blocks. Returns the left channel.
std::vector<float> run(const mensis::FxParams& p, int block = 512) {
    mensis::FxRack rack;
    rack.prepare(kRate, block);
    const int n = static_cast<int>(4 * kRate);
    std::vector<float> l(static_cast<size_t>(n)), r(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
            0.5f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / kRate));
    for (int off = 0; off < n; off += block)
        rack.process(l.data() + off, r.data() + off, std::min(block, n - off), p, 120.0);
    return l;
}

} // namespace

TEST_CASE("fx rack with everything off passes the input through") {
    const auto out = run({});
    for (size_t i = 0; i < out.size(); ++i)
        REQUIRE(std::abs(out[i] - 0.5f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / kRate))) < 1e-4f);
}

TEST_CASE("fx rack grains on live input: sound, no clicks, repeatable") {
    mensis::FxParams p;
    p.grains.mix = 1.0f;
    p.grains.spray = 1.0f;   // widest reach into the recording
    p.grains.detune = 12.0f; // fastest grains: must still only read what's already recorded
    p.grains.reverse = 0.5f;
    p.grains.sizeMs = 500.0f;
    p.grains.chaos = 1.0f;
    p.send = 0.5f;
    const auto out = run(p, 333); // odd block size on purpose
    const auto s = mensis::measure(out);
    CHECK(s.finite);
    CHECK(s.peak > 0.05f);  // the cloud makes sound
    CHECK(s.peak <= 1.5f);
    CHECK(s.maxStep < 0.1f); // a 220 Hz sine at 0.5 moves ~0.015 per sample; reading unrecorded silence would jump
    CHECK(run(p, 333) == out);
}
