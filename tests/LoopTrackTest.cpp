#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/RenderStats.h"
#include "core/engine/LoopTrack.h"

using Catch::Matchers::WithinAbs;

namespace {

constexpr double kRate = 48000.0, kBpm = 120.0;
constexpr int kLen = 1000;

// Mono ramp: sample i has value i / kLen, so the output value tells us the read position.
mensis::Sample rampSample() {
    mensis::Sample s;
    s.audio.setSize(1, kLen);
    for (int i = 0; i < kLen; ++i) s.audio.setSample(0, i, static_cast<float>(i) / kLen);
    return s;
}

float renderOne(const mensis::LoopParams& p, double ppq) {
    mensis::LoopTrack track;
    track.prepare(kRate);
    juce::AudioBuffer<float> out(2, 1);
    out.clear();
    const auto s = rampSample();
    track.render(s, p, ppq, kBpm, out, 0, 1);
    return out.getSample(0, 0);
}

} // namespace

TEST_CASE("loop position follows the host beat position") {
    mensis::LoopParams p;
    p.levelDb = 0.0f;
    p.bars = 1.0; // 4 beats
    // Beat 2 of a 4-beat loop = halfway through the sample.
    CHECK_THAT(renderOne(p, 2.0), WithinAbs(0.4995, 1e-3));
    // Beat 6 = one loop later, same place.
    CHECK_THAT(renderOne(p, 6.0), WithinAbs(0.4995, 1e-3));
    // Two bars: beat 2 is a quarter of the way through.
    p.bars = 2.0;
    CHECK_THAT(renderOne(p, 2.0), WithinAbs(0.24975, 1e-3));
}

TEST_CASE("reverse reads backwards, start/length pick the region") {
    mensis::LoopParams p;
    p.levelDb = 0.0f;
    p.reverse = true;
    CHECK_THAT(renderOne(p, 1.0), WithinAbs(0.74925, 1e-3));
    p.reverse = false;
    p.start = 0.5f;
    p.length = 0.5f; // half of what's left: region is samples 499.5 .. 749.25
    CHECK_THAT(renderOne(p, 0.0), WithinAbs(0.0, 1e-6)); // seam fade: silent exactly at the loop start
    CHECK_THAT(renderOne(p, 2.0), WithinAbs(0.6244, 1e-3));
}

TEST_CASE("no click at the loop seam") {
    // A ramp jumps from ~1 back to 0 at the seam. The seam fade must smooth that out.
    mensis::LoopParams p;
    p.levelDb = 0.0f;
    mensis::LoopTrack track;
    track.prepare(kRate);
    juce::AudioBuffer<float> out(2, 2000);
    out.clear();
    const auto s = rampSample();
    track.render(s, p, 4.0 - 1000 * kBpm / (60.0 * kRate), kBpm, out, 0, 2000); // straddles beat 4
    const auto stats = mensis::measure({out.getReadPointer(0), 2000});
    CHECK(stats.finite);
    CHECK(stats.maxStep < 0.01f);
}

TEST_CASE("pan is a balance: centre keeps both sides at full level") {
    mensis::LoopParams p;
    p.levelDb = 0.0f;
    p.pan = -1.0f;
    mensis::LoopTrack track;
    track.prepare(kRate);
    juce::AudioBuffer<float> out(2, 1);
    out.clear();
    const auto s = rampSample();
    track.render(s, p, 2.0, kBpm, out, 0, 1);
    CHECK_THAT(out.getSample(0, 0), WithinAbs(0.4995, 1e-3));
    CHECK(out.getSample(1, 0) == 0.0f);
}
