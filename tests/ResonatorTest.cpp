#include <catch2/catch_test_macros.hpp>
#include "core/RenderStats.h"
#include "core/engine/Resonator.h"

namespace {

constexpr double kRate = 48000.0;
constexpr float kPi = juce::MathConstants<float>::pi;

// Energy of x at one frequency (Goertzel).
double energyAt(const std::vector<float>& x, double hz) {
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / kRate, c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (float v : x) {
        const double s = v + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

// A 10 ms noise burst, then silence; returns what rings on afterwards (mono, left).
std::vector<float> ringAfterBurst(const mensis::ResonatorParams& p, double seconds = 1.0) {
    mensis::Resonator res;
    res.prepare(kRate);
    juce::Random rng(5);
    std::vector<float> out;
    for (int i = 0; i < static_cast<int>((0.01 + seconds) * kRate); ++i) {
        float l = i < 480 ? rng.nextFloat() * 2.0f - 1.0f : 0.0f, r = l;
        res.process(l, r, p);
        if (i >= 480) out.push_back(l);
    }
    return out;
}

mensis::ResonatorParams wet(float root, float color = 0.0f) {
    mensis::ResonatorParams p;
    p.mix = 1.0f;
    p.root = root;
    p.decay = 2.0f;
    p.color = color;
    return p;
}

} // namespace

TEST_CASE("resonator mix 0 leaves the sound untouched") {
    mensis::Resonator res;
    res.prepare(kRate);
    mensis::ResonatorParams p; // mix 0
    juce::Random rng(1);
    for (int i = 0; i < 4800; ++i) {
        const float x = rng.nextFloat() - 0.5f;
        float l = x, r = -x;
        res.process(l, r, p);
        REQUIRE(l == x);
        REQUIRE(r == -x);
    }
}

TEST_CASE("resonator rings at the root note") {
    const auto ring = ringAfterBurst(wet(57.0f)); // A2 in Live = 220 Hz
    const double at = energyAt(ring, 220.0);
    CHECK(at > 10.0 * energyAt(ring, 233.1)); // a semitone up
    CHECK(at > 10.0 * energyAt(ring, 207.7)); // a semitone down
}

TEST_CASE("MIDI notes take over from root + chord") {
    auto p = wet(57.0f);
    p.midiNotes = {64.0f}; // E3-ish, 329.6 Hz
    p.numMidiNotes = 1;
    const auto ring = ringAfterBurst(p);
    CHECK(energyAt(ring, 329.6) > 10.0 * energyAt(ring, 220.0));
}

TEST_CASE("color morphs string partials into bell partials") {
    const auto string = ringAfterBurst(wet(57.0f, 0.0f)), bell = ringAfterBurst(wet(57.0f, 1.0f));
    const double bellPartial = 220.0 * 2.756; // 2nd free-bar mode
    CHECK(energyAt(bell, bellPartial) > 10.0 * energyAt(string, bellPartial));
    CHECK(energyAt(string, 440.0) > 10.0 * energyAt(bell, 440.0)); // the harmonic 2nd partial is gone
}

TEST_CASE("resonator stays bounded and rings out") {
    for (float decay : {0.2f, 10.0f}) {
        auto p = wet(45.0f);
        p.chord = 3; // Major: 4 notes
        p.decay = decay;
        mensis::Resonator res;
        res.prepare(kRate);
        juce::Random rng(2);
        std::vector<float> out;
        const int loud = static_cast<int>(2.0 * kRate), total = loud + static_cast<int>(1.0 * kRate);
        double inSq = 0.0, outSq = 0.0;
        for (int i = 0; i < total; ++i) {
            float l = i < loud ? rng.nextFloat() * 2.0f - 1.0f : 0.0f, r = l;
            if (i < loud) inSq += l * l;
            res.process(l, r, p);
            if (i < loud) outSq += l * l;
            out.push_back(l);
        }
        const auto stats = mensis::measure(out);
        CHECK(stats.finite);
        CHECK(stats.peak <= 1.0f);
        const double ratio = std::sqrt(outSq / inSq); // full-scale noise in: comes out at a similar level
        CHECK(ratio > 0.2);
        CHECK(ratio < 2.0);
        if (decay < 1.0f) {
            float tail = 0.0f;
            for (size_t i = out.size() - 4800; i < out.size(); ++i) tail = std::max(tail, std::abs(out[i]));
            CHECK(tail < 1e-3f); // 0.2 s decay: silent 1 s later
        }
    }
}

TEST_CASE("retuning glides: no click when the root jumps") {
    auto p = wet(45.0f);
    p.decay = 5.0f;
    mensis::Resonator res;
    res.prepare(kRate);
    juce::Random rng(3);
    std::vector<float> out;
    for (int i = 0; i < static_cast<int>(2.0 * kRate); ++i) {
        p.root = i < 48000 ? 45.0f : 52.0f;
        float l = 0.3f * (rng.nextFloat() * 2.0f - 1.0f), r = l;
        res.process(l, r, p);
        out.push_back(l);
    }
    std::vector<float> around(out.begin() + 47000, out.begin() + 50000), before(out.begin() + 40000, out.begin() + 47000);
    CHECK(mensis::measure(around).maxStep < 1.5f * mensis::measure(before).maxStep + 0.01f);
}

TEST_CASE("morph filter: low-pass, band-pass, high-pass, and open = untouched") {
    auto gainOf = [](const mensis::FilterParams& p, float hz) {
        mensis::MorphFilter f;
        f.prepare(kRate);
        float peak = 0.0f;
        for (int i = 0; i < 48000; ++i) {
            const float x = std::sin(2.0f * kPi * hz * static_cast<float>(i) / static_cast<float>(kRate));
            float l = x, r = x;
            f.process(l, r, p);
            if (i > 24000) peak = std::max(peak, std::abs(l));
        }
        return peak;
    };
    const mensis::FilterParams lp{500.0f, 0.0f, 0.0f}, bp{1000.0f, 0.0f, 0.5f}, hp{5000.0f, 0.0f, 1.0f};
    CHECK(gainOf(lp, 5000.0f) < 0.1f);  // -20 dB
    CHECK(gainOf(lp, 100.0f) > 0.9f);
    CHECK(gainOf(hp, 200.0f) < 0.1f);
    CHECK(gainOf(hp, 15000.0f) > 0.9f);
    CHECK(gainOf(bp, 1000.0f) > 0.8f);  // passes its centre
    CHECK(gainOf(bp, 100.0f) < 0.25f);

    mensis::MorphFilter open;
    open.prepare(kRate);
    for (int i = 0; i < 1000; ++i) {
        const float x = static_cast<float>(i % 17) / 17.0f - 0.5f;
        float l = x, r = x;
        open.process(l, r, mensis::FilterParams{});
        REQUIRE(l == x);
    }
}
