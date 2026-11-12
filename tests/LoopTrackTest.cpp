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

TEST_CASE("warp keeps the pitch when the loop is stretched") {
    // 1 s of 440 Hz, looped over 1 bar at 120 BPM = 2 s: tape mode would play it at 220 Hz.
    mensis::Sample s;
    s.sampleRate = kRate;
    s.audio.setSize(1, static_cast<int>(kRate));
    for (int i = 0; i < s.audio.getNumSamples(); ++i)
        s.audio.setSample(0, i, std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * i / static_cast<float>(kRate)));

    auto crossingsPerSecond = [&](bool warp, float pitch = 0.0f) {
        mensis::LoopParams p;
        p.levelDb = 0.0f;
        p.warp = warp;
        p.pitch = pitch;
        mensis::LoopTrack track;
        track.prepare(kRate);
        const int len = static_cast<int>(kRate); // render 1 s from beat 0.5, away from the seam
        juce::AudioBuffer<float> out(2, len);
        out.clear();
        for (int pos = 0; pos < len; pos += 512)
            track.render(s, p, 0.5 + pos * kBpm / (60.0 * kRate), kBpm, out, pos, std::min(512, len - pos));
        const auto stats = mensis::measure({out.getReadPointer(0), static_cast<size_t>(len)});
        CHECK(stats.finite);
        CHECK(stats.peak <= 1.0f);
        // A 440 Hz sine steps at most ~0.058 per sample (more when pitched up): no clicks between grains.
        CHECK(stats.maxStep < 0.1f * std::exp2(std::max(0.0f, pitch) / 12.0f));
        int crossings = 0;
        for (int i = 1; i < len; ++i) crossings += (out.getSample(0, i - 1) < 0.0f) != (out.getSample(0, i) < 0.0f);
        return crossings / 2.0;
    };
    CHECK_THAT(crossingsPerSecond(false), WithinAbs(220.0, 3.0));
    // Overlapping grains of a pure sine partly cancel now and then, which adds a few extra crossings.
    CHECK_THAT(crossingsPerSecond(true), WithinAbs(440.0, 25.0));
    // Pitch moves it in Warp without changing the timing: +12 = one octave up, -12 = one down.
    CHECK_THAT(crossingsPerSecond(true, 12.0f), WithinAbs(880.0, 50.0));
    CHECK_THAT(crossingsPerSecond(true, -12.0f), WithinAbs(220.0, 15.0));
}

TEST_CASE("sliced + warp plays hits Beats-style: on time and crisp at another tempo") {
    // 1 bar at 120 BPM (2 s): four short 1 kHz pings, one per beat.
    mensis::Sample s;
    s.sampleRate = kRate;
    s.audio.setSize(1, static_cast<int>(2.0 * kRate));
    s.audio.clear();
    for (int hit = 0; hit < 4; ++hit)
        for (int i = 0; i < 4800; ++i)
            s.audio.setSample(0, hit * 24000 + i, std::sin(2.0f * juce::MathConstants<float>::pi * 1000.0f * i / 48000.0f)
                                                   * std::exp(-i / 600.0f));
    s.slices = {0, 24000, 48000, 72000};

    for (double bpm : {90.0, 150.0}) { // slower and faster than the sample's own 120
        mensis::LoopParams p;
        p.levelDb = 0.0f;
        p.warp = true;
        p.slice = true;
        mensis::LoopTrack track;
        track.prepare(kRate);
        const double beatSec = 60.0 / bpm;
        const int len = static_cast<int>(4.0 * beatSec * kRate);
        juce::AudioBuffer<float> out(2, len);
        out.clear();
        for (int pos = 0; pos < len; pos += 512)
            track.render(s, p, pos * bpm / (60.0 * kRate), bpm, out, pos, std::min(512, len - pos));

        for (int beat = 1; beat < 4; ++beat) {
            const int at = static_cast<int>(beat * beatSec * kRate);
            float hitPeak = 0.0f, before = 0.0f;
            for (int i = at; i < at + 96; ++i) hitPeak = std::max(hitPeak, std::abs(out.getSample(0, i)));        // first 2 ms
            for (int i = at - 480; i < at - 96; ++i) before = std::max(before, std::abs(out.getSample(0, i)));    // 10..2 ms before
            CHECK(hitPeak > 0.9f);   // the attack is there right on the beat, at full level
            CHECK(before < 0.05f);   // and nothing smeared in before it
        }
        const auto stats = mensis::measure({out.getReadPointer(0), static_cast<size_t>(len)});
        CHECK(stats.maxStep < 0.2f); // a 1 kHz ping steps up to ~0.13 per sample: no clicks
    }
}
