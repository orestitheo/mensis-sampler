#include <catch2/catch_test_macros.hpp>
#include "core/RenderStats.h"
#include "core/engine/Color.h"

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi = juce::MathConstants<double>::pi;

double energyAt(const std::vector<float>& x, double hz) { // Goertzel
    const double c = 2.0 * std::cos(2.0 * kPi * hz / kRate);
    double s1 = 0.0, s2 = 0.0;
    for (float v : x) {
        const double s = v + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

std::vector<float> run(const mensis::ColorParams& p, const std::function<float(int)>& in, int n) {
    mensis::Color color;
    color.prepare(kRate);
    std::vector<float> out;
    for (int i = 0; i < n; ++i) {
        float l = in(i), r = l;
        color.process(l, r, p);
        out.push_back(l);
    }
    return out;
}

float sine(double hz, int i, float amp = 0.5f) { return amp * static_cast<float>(std::sin(2.0 * kPi * hz * i / kRate)); }

// Drum-ish test signal: noise bursts that decay, 4 per second.
float drums(int i) {
    static juce::Random rng(7);
    const int phase = i % 12000;
    return (rng.nextFloat() * 2.0f - 1.0f) * 0.8f * std::exp(-phase / 1500.0f);
}

double rms(const std::vector<float>& x, size_t from = 0) {
    double s = 0.0;
    for (size_t i = from; i < x.size(); ++i) s += x[i] * x[i];
    return std::sqrt(s / static_cast<double>(x.size() - from));
}

} // namespace

TEST_CASE("color at neutral settings leaves the sound untouched") {
    mensis::Color color;
    color.prepare(kRate);
    for (int i = 0; i < 4800; ++i) {
        const float x = sine(440.0, i);
        float l = x, r = x;
        color.process(l, r, mensis::ColorParams{});
        REQUIRE(l == x);
    }
}

TEST_CASE("extreme color settings stay finite and bounded") {
    mensis::ColorParams p{1.0f, 1.0f, 1.0f, 200.0f, 1.0f, 1.0f, 12.0f, 1.0f};
    juce::Random rng(1);
    const auto out = run(p, [&](int) { return rng.nextFloat() * 4.0f - 2.0f; }, 48000); // hot input, +-2
    const auto stats = mensis::measure(out);
    CHECK(stats.finite);
    CHECK(stats.peak < 16.0f); // bounded: shaper output * at most +12 dB out, +6 dB tilt, squash make-up
}

TEST_CASE("anti-aliased drive folds back far less junk than a plain tanh") {
    // 5 kHz driven hard: its 9th harmonic (45 kHz) folds back to 3 kHz. Compare with naive tanh.
    const double drive = 30.0;
    std::vector<float> naive, adaa;
    float prev = 0.0f;
    for (int i = 0; i < 48000; ++i) {
        const float in = sine(5000.0, i) * static_cast<float>(drive);
        naive.push_back(std::tanh(in));
        adaa.push_back(mensis::Color::shape(in, prev, 0.0f));
        prev = in;
    }
    const double fundamental = energyAt(adaa, 5000.0);
    CHECK(energyAt(adaa, 3000.0) < 0.25 * energyAt(naive, 3000.0)); // at least 6 dB less aliasing
    CHECK(energyAt(adaa, 3000.0) < 0.01 * fundamental);              // and 20 dB under the note
}

TEST_CASE("drive saturates without a big jump in loudness") {
    const auto dry = run(mensis::ColorParams{}, drums, 96000);
    for (float c : {0.0f, 1.0f})
        for (float d : {0.5f, 1.0f}) {
            mensis::ColorParams p;
            p.drive = d;
            p.character = c;
            const double ratio = rms(run(p, drums, 96000)) / rms(dry);
            CHECK(ratio > 0.5); // within about +-6 dB
            CHECK(ratio < 2.0);
        }
}

TEST_CASE("squash evens out loud and quiet parts") {
    auto crest = [](const std::vector<float>& x) {
        float peak = 0.0f;
        for (float v : x) peak = std::max(peak, std::abs(v));
        return peak / rms(x);
    };
    mensis::ColorParams p;
    p.squash = 0.8f;
    const auto dry = run(mensis::ColorParams{}, drums, 96000), squashed = run(p, drums, 96000);
    CHECK(crest(squashed) < 0.8 * crest(dry));
    const double ratio = rms(squashed, 24000) / rms(dry, 24000);
    CHECK(ratio > 0.5);
    CHECK(ratio < 2.0);
}

TEST_CASE("tilt makes it darker or brighter") {
    auto gainOf = [](float tilt, double hz) {
        mensis::ColorParams p;
        p.tilt = tilt;
        const auto out = run(p, [hz](int i) { return sine(hz, i); }, 48000);
        float peak = 0.0f;
        for (size_t i = 24000; i < out.size(); ++i) peak = std::max(peak, std::abs(out[i]));
        return peak / 0.5f;
    };
    CHECK(gainOf(1.0f, 10000.0) > 1.6f); // ~ +6 dB
    CHECK(gainOf(1.0f, 60.0) < 0.6f);    // ~ -6 dB
    CHECK(gainOf(-1.0f, 10000.0) < 0.6f);
    CHECK(gainOf(-1.0f, 60.0) > 1.6f);
}

TEST_CASE("bits and rate crush") {
    mensis::ColorParams p;
    p.bits = 2.0f; // 2 steps per side: only -1, -0.5, 0, 0.5, 1
    const auto crushed = run(p, [](int i) { return sine(100.0, i, 0.9f); }, 4800);
    for (float v : crushed) CHECK(std::abs(v * 2.0f - std::round(v * 2.0f)) < 1e-6f);
    p = {};
    p.rateHz = 1000.0f; // holds each value for 48 samples
    const auto held = run(p, [](int i) { return sine(10.0, i); }, 4800);
    int changes = 0;
    for (size_t i = 1; i < held.size(); ++i) changes += held[i] != held[i - 1];
    CHECK(changes >= 95);
    CHECK(changes <= 101);
}
