#include <catch2/catch_test_macros.hpp>
#include "core/RenderStats.h"
#include "core/engine/Eq.h"

namespace {

constexpr double kRate = 48000.0;

// Gain in dB of a steady sine at hz through the EQ.
double gainDb(const mensis::EqParams& p, double hz) {
    mensis::Eq eq;
    eq.prepare(kRate);
    double in = 0.0, out = 0.0; // energy, not peak: at 16 kHz there are only 3 samples per cycle
    for (int i = 0; i < 48000; ++i) {
        const float x = 0.25f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / kRate));
        float l = x, r = x;
        eq.process(l, r, p);
        if (i >= 24000) { in += x * x; out += l * l; }
    }
    return 10.0 * std::log10(out / in);
}

} // namespace

TEST_CASE("eq at 0 dB leaves the sound untouched") {
    mensis::Eq eq;
    eq.prepare(kRate);
    juce::Random rng(3);
    mensis::EqParams p;
    p.midHz = 3000.0f; // the frequency alone changes nothing
    for (int i = 0; i < 4800; ++i) {
        const float in = rng.nextFloat() - 0.5f;
        float l = in, r = in;
        eq.process(l, r, p);
        REQUIRE(l == in);
        REQUIRE(r == in);
    }
}

TEST_CASE("eq bands boost and cut where they should, and leave the rest alone") {
    mensis::EqParams low, mid, high;
    low.low = 12.0f;
    mid.mid = -12.0f;
    mid.midHz = 1000.0f;
    high.high = 12.0f;
    CHECK(std::abs(gainDb(low, 40.0) - 12.0) < 1.0);
    CHECK(std::abs(gainDb(low, 3000.0)) < 0.5);
    CHECK(std::abs(gainDb(mid, 1000.0) + 12.0) < 0.5);
    CHECK(std::abs(gainDb(mid, 60.0)) < 0.5);
    CHECK(std::abs(gainDb(mid, 12000.0)) < 1.0);
    CHECK(std::abs(gainDb(high, 16000.0) - 12.0) < 1.0);
    CHECK(std::abs(gainDb(high, 300.0)) < 0.5);
    mid.midHz = 4000.0f; // MID FREQ moves the bell
    CHECK(std::abs(gainDb(mid, 4000.0) + 12.0) < 0.5);
    CHECK(std::abs(gainDb(mid, 1000.0)) < 2.5);
}

TEST_CASE("sweeping every eq knob at full speed stays clean: no clicks, no NaN") {
    mensis::Eq eq;
    eq.prepare(kRate);
    std::vector<float> out;
    for (int i = 0; i < 96000; ++i) {
        const int step = i / 2400; // new settings 20 times a second
        mensis::EqParams p;
        p.low = step % 2 ? 12.0f : -12.0f;
        p.mid = step % 3 ? -12.0f : 12.0f;
        p.midHz = step % 4 ? 200.0f : 8000.0f;
        p.high = step % 5 ? 12.0f : 0.0f;
        float l = 0.05f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / kRate)), r = l;
        eq.process(l, r, p);
        out.push_back(l);
    }
    const auto s = mensis::measure(out);
    CHECK(s.finite);
    INFO("peak " << s.peak << ", maxStep " << s.maxStep);
    CHECK(s.peak < 0.5f);     // 0.05 in, at most ~+16 dB (low shelf + bell both on 220 Hz) = 0.32
    CHECK(s.maxStep < 0.02f); // that sine steps 0.009 per sample at its loudest; a click would be far bigger
}
