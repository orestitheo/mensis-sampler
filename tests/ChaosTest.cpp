#include <catch2/catch_test_macros.hpp>
#include "core/RenderStats.h"
#include "core/engine/LoopTrack.h"

namespace {

constexpr double kLoopBeats = 4.0;

// One number per 1/64 note over 16 bars, so two patterns can be compared.
std::vector<double> pattern(const mensis::ChaosParams& p) {
    std::vector<double> v;
    for (double b = 0.0; b < 64.0; b += 1.0 / 16.0) {
        const auto r = mensis::chaosAt(p, kLoopBeats, b + 0.01);
        v.push_back(r.beats * r.dir + r.gain * 1000.0);
    }
    return v;
}

int changedSteps(const mensis::ChaosParams& p) {
    int n = 0;
    for (double b = 0.0; b < 64.0; b += 0.25) {
        const auto r = mensis::chaosAt(p, kLoopBeats, b + 0.1);
        n += r.beats != b + 0.1 || r.gain != 1.0f;
    }
    return n;
}

mensis::Sample sine() {
    mensis::Sample s;
    s.sampleRate = 48000.0;
    s.audio.setSize(2, 96000);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 96000; ++i) s.audio.setSample(ch, i, 0.5f * std::sin(0.0576f * static_cast<float>(i))); // ~440 Hz
    return s;
}

juce::AudioBuffer<float> render(const mensis::LoopParams& p, int seconds) {
    const auto s = sine();
    mensis::LoopTrack track;
    track.prepare(48000.0);
    juce::AudioBuffer<float> out(2, 48000 * seconds);
    out.clear();
    for (int pos = 0; pos < out.getNumSamples(); pos += 512)
        track.render(s, p, pos * 120.0 / (60.0 * 48000.0), 120.0, out, pos, std::min(512, out.getNumSamples() - pos));
    return out;
}

} // namespace

TEST_CASE("chaos 0 leaves the loop alone") {
    mensis::ChaosParams p;
    p.seed = p.nextSeed = 7;
    CHECK(changedSteps(p) == 0);
    p.amount = 0.5f;
    CHECK(changedSteps(p) > 0);
}

TEST_CASE("same seed = same glitches, new seed = new glitches") {
    mensis::ChaosParams p;
    p.amount = 0.7f;
    p.seed = p.nextSeed = 1;
    CHECK(pattern(p) == pattern(p));
    auto q = p;
    q.seed = q.nextSeed = 2;
    CHECK(pattern(p) != pattern(q));
    q.stream = 1; // another track with the same seed glitches differently
    q.seed = q.nextSeed = 1;
    CHECK(pattern(p) != pattern(q));
}

TEST_CASE("more chaos = more events") {
    mensis::ChaosParams p;
    p.seed = p.nextSeed = 3;
    p.amount = 0.2f;
    const int low = changedSteps(p);
    p.amount = 1.0f;
    CHECK(changedSteps(p) > low * 2);
}

TEST_CASE("re-roll only changes steps from the switch point on") {
    mensis::ChaosParams a;
    a.amount = 0.8f;
    a.seed = a.nextSeed = 1;
    auto b = a;
    b.nextSeed = 2;
    b.switchBeats = 32.0;
    const auto pa = pattern(a), pb = pattern(b);
    // Up to beat 31 (events last at most 1 beat at 1/16 grid) nothing changes.
    CHECK(std::equal(pa.begin(), pa.begin() + 31 * 16, pb.begin()));
    CHECK(!std::equal(pa.begin() + 33 * 16, pa.end(), pb.begin() + 33 * 16));
}

TEST_CASE("full chaos renders without clicks, in tape and warp mode, and repeats exactly") {
    for (bool warp : {false, true}) {
        mensis::LoopParams p;
        p.levelDb = 0.0f;
        p.warp = warp;
        p.chaos.amount = 1.0f;
        p.chaos.seed = p.chaos.nextSeed = 42;
        const auto out = render(p, 8);
        const auto stats = mensis::measure({out.getReadPointer(0), static_cast<size_t>(out.getNumSamples())});
        CHECK(stats.finite);
        CHECK(stats.peak <= 0.51f);
        CHECK(stats.maxStep < 0.1f); // the clean sine steps up to ~0.03; a click would be far bigger
        const auto again = render(p, 8);
        bool same = true;
        for (int i = 0; i < out.getNumSamples(); ++i) same = same && out.getSample(0, i) == again.getSample(0, i);
        CHECK(same);
    }
}
