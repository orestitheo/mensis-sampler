#pragma once
#include <array>
#include <juce_audio_basics/juce_audio_basics.h>

namespace mensis {

struct ColorParams {
    float drive = 0.0f;      // 0..1 -> 0..36 dB into the shaper
    float character = 0.0f;  // 0 = warm (tanh), 1 = fuzz (hard clip)
    float bits = 16.0f;      // 16 = off .. 1
    float rateHz = 48000.0f; // sample-and-hold rate; at or above the host rate = off
    float squash = 0.0f;     // 0..1 one-knob compressor
    float tilt = 0.0f;       // -1 dark .. +1 bright
    float outDb = 0.0f;      // -12..+12
    float mix = 1.0f;        // dry/wet of the whole Color chain
};

// Drive, bit-crush, sample-rate reduction, compression and tilt EQ for one track. Real-time safe.
class Color {
public:
    void prepare(double sampleRate);
    void process(float& l, float& r, const ColorParams& p); // one stereo sample, in place

    // The shaper, exposed for tests: anti-aliased (ADAA) drive of x given the previous input.
    static float shape(float x, float prev, float character);

private:
    double sampleRate = 48000.0;
    bool primed = false;
    std::array<float, 2> prevIn{}, held{}, tiltLow{};
    double holdPhase = 1.0;
    float envelope = 0.0f; // compressor level (linear)
    juce::SmoothedValue<float> drive, tilt, outGain, mix;
};

} // namespace mensis
