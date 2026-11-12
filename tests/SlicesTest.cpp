#include <catch2/catch_test_macros.hpp>
#include "core/engine/Slices.h"

namespace {

constexpr double kRate = 44100.0;

// A 2-bar beat at 120 BPM: kick (falling sine) on 1 and 3, snare (noise) on 2 and 4, quiet hats on the 8ths
// in between. Returns the audio and where each hit starts.
std::pair<juce::AudioBuffer<float>, std::vector<int>> drumLoop() {
    const int len = static_cast<int>(4.0 * kRate), eighth = static_cast<int>(0.25 * kRate);
    juce::AudioBuffer<float> audio(2, len);
    audio.clear();
    juce::Random rng(1);
    std::vector<int> hits;
    for (int k = 0; k < 16; ++k) {
        const int at = k * eighth;
        hits.push_back(at);
        const bool kick = k % 4 == 0, snare = k % 4 == 2;
        for (int i = 0; i < eighth && at + i < len; ++i) {
            const double t = i / kRate;
            float v;
            if (kick) v = 0.9f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * (50.0 * t + 60.0 * (1.0 - std::exp(-t * 30.0)) / 30.0)) * std::exp(-t * 12.0));
            else if (snare) v = 0.6f * (rng.nextFloat() * 2.0f - 1.0f) * static_cast<float>(std::exp(-t * 25.0));
            else v = 0.15f * (rng.nextFloat() * 2.0f - 1.0f) * static_cast<float>(std::exp(-t * 80.0));
            for (int ch = 0; ch < 2; ++ch) audio.setSample(ch, at + i, v);
        }
    }
    return {audio, hits};
}

} // namespace

TEST_CASE("finds every hit of a drum loop, right where it starts") {
    const auto [audio, hits] = drumLoop();
    const auto info = mensis::findSlices(audio, kRate);
    REQUIRE(info.onsets.size() == hits.size());
    for (size_t i = 0; i < hits.size(); ++i) {
        const int err = info.onsets[i] - hits[i];
        CHECK(err <= 0);                                  // never late: the attack must not be cut
        CHECK(err >= -static_cast<int>(0.003 * kRate));   // and at most 3 ms early
    }
    CHECK(info.drumLike);
}

TEST_CASE("a held tone is not drums") {
    juce::AudioBuffer<float> pad(1, static_cast<int>(4.0 * kRate));
    for (int i = 0; i < pad.getNumSamples(); ++i) {
        const double t = i / kRate; // two slow-beating tones with a soft fade in
        pad.setSample(0, i, static_cast<float>(0.3 * std::min(1.0, t) * (std::sin(2 * juce::MathConstants<double>::pi * 220 * t)
                                                                         + std::sin(2 * juce::MathConstants<double>::pi * 221.5 * t))));
    }
    const auto info = mensis::findSlices(pad, kRate);
    CHECK(info.onsets.size() <= 1);
    CHECK_FALSE(info.drumLike);
}

TEST_CASE("silence has no slices") {
    juce::AudioBuffer<float> silence(2, 44100);
    silence.clear();
    const auto info = mensis::findSlices(silence, kRate);
    CHECK(info.onsets.empty());
    CHECK_FALSE(info.drumLike);
}
