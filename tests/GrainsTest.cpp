#include <catch2/catch_test_macros.hpp>
#include "core/RenderStats.h"
#include "core/engine/LoopTrack.h"

namespace {

constexpr double kRate = 48000.0;

mensis::Sample sine(float hz, double seconds) {
    mensis::Sample s;
    s.sampleRate = kRate;
    s.audio.setSize(2, static_cast<int>(seconds * kRate));
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < s.audio.getNumSamples(); ++i)
            s.audio.setSample(ch, i, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * hz * i / static_cast<float>(kRate)));
    return s;
}

juce::AudioBuffer<float> render(const mensis::Sample& s, const mensis::LoopParams& p, double seconds) {
    mensis::LoopTrack track;
    track.prepare(kRate);
    juce::AudioBuffer<float> out(2, static_cast<int>(seconds * kRate));
    out.clear();
    for (int pos = 0; pos < out.getNumSamples(); pos += 512)
        track.render(s, p, pos * 120.0 / (60.0 * kRate), 120.0, out, pos, std::min(512, out.getNumSamples() - pos));
    return out;
}

bool same(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (a.getSample(ch, i) != b.getSample(ch, i)) return false;
    return true;
}

mensis::LoopParams wildCloud() {
    mensis::LoopParams p;
    p.levelDb = 0.0f;
    p.warp = true;
    p.grains.mix = 1.0f;
    p.grains.sizeMs = 60.0f;
    p.grains.density = 60.0f;
    p.grains.spray = 0.8f;
    p.grains.detune = 12.0f;
    p.grains.reverse = 0.5f;
    p.grains.width = 1.0f;
    p.grains.chaos = 1.0f;
    p.grains.seed = 9;
    return p;
}

} // namespace

TEST_CASE("grain mix 0 leaves the loop untouched") {
    const auto s = sine(440.0f, 2.0);
    mensis::LoopParams plain;
    plain.warp = true;
    auto zero = wildCloud();
    zero.grains.mix = 0.0f;
    zero.levelDb = plain.levelDb;
    CHECK(same(render(s, plain, 2.0), render(s, zero, 2.0)));
}

TEST_CASE("a wild cloud stays clean: no clicks, no NaN, sane level, same seed = same cloud") {
    const auto s = sine(440.0f, 2.0);
    const auto p = wildCloud();
    const auto out = render(s, p, 6.0);
    for (int ch = 0; ch < 2; ++ch) {
        const auto stats = mensis::measure({out.getReadPointer(ch), static_cast<size_t>(out.getNumSamples())});
        CHECK(stats.finite);
        CHECK(stats.peak < 1.0f);
        CHECK(stats.peak > 0.1f);
        // 0.5 * a 880 Hz sine (440 + 12 st) steps ~0.058 per sample; overlapping grains add a little.
        CHECK(stats.maxStep < 0.15f);
    }
    CHECK(same(out, render(s, p, 6.0)));
    auto other = p;
    other.grains.seed = 10;
    CHECK_FALSE(same(out, render(s, other, 6.0)));
}

TEST_CASE("freeze holds the cloud on one spot while the loop moves on") {
    // Ramp sample: the value tells where a grain is reading. 4 s long, looped over 2 bars (4 s at 120 BPM).
    mensis::Sample s;
    s.sampleRate = kRate;
    const int n = static_cast<int>(4.0 * kRate);
    s.audio.setSize(1, n);
    for (int i = 0; i < n; ++i) s.audio.setSample(0, i, static_cast<float>(i) / static_cast<float>(n));

    auto p = wildCloud();
    p.warp = false; // tape: the loop reads the ramp at exactly real speed
    p.bars = 2.0;
    p.grains = {};
    p.grains.mix = 1.0f;
    p.grains.sizeMs = 20.0f;
    p.grains.density = 10.0f; // grains don't overlap: each one's peak ~ the ramp where it started
    p.grains.spray = 0.0f;
    p.grains.width = 0.0f;

    mensis::LoopTrack track;
    track.prepare(kRate);
    juce::AudioBuffer<float> out(2, n);
    out.clear();
    for (int pos = 0; pos < n; pos += 512) {
        p.grains.freeze = pos >= static_cast<int>(1.0 * kRate); // freeze after 1 s
        track.render(s, p, pos * 120.0 / (60.0 * kRate), 120.0, out, pos, std::min(512, n - pos));
    }
    float lateMax = 0.0f;
    for (int i = static_cast<int>(3.0 * kRate); i < n; ++i) lateMax = std::max(lateMax, out.getSample(0, i));
    // Frozen at ~1 s => ramp ~0.25 (+ one 20 ms grain). Without freeze it would be up near 1.0.
    CHECK(lateMax > 0.2f);
    CHECK(lateMax < 0.3f);
}
