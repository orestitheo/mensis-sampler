#pragma once
#include <array>
#include <juce_audio_basics/juce_audio_basics.h>

namespace mensis {

struct EqParams {
    float low = 0.0f;      // dB, -12..+12: shelf below 150 Hz
    float mid = 0.0f;      // dB, -12..+12: bell at midHz
    float midHz = 1000.0f; // 200..8000
    float high = 0.0f;     // dB, -12..+12: shelf above 6 kHz
};

// 3-band EQ for one track: low shelf, mid bell, high shelf. All gains at 0 = bypassed, bit-identical.
// Same filter form as MorphFilter (Cytomic/Simper), so it stays stable while knobs move. Real-time safe.
class Eq {
public:
    void prepare(double sampleRate);
    void process(float& l, float& r, const EqParams& p); // one stereo sample, in place

private:
    static constexpr int kUpdateEvery = 4; // 16 gave audible steps when a knob moves fast
    struct Band {
        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;
        std::array<float, 2> ic1{}, ic2{}; // per channel
    };
    void updateCoefficients(float lowDb, float midDb, float midHz, float highDb);

    double sampleRate = 48000.0;
    std::array<Band, 3> bands{};
    juce::SmoothedValue<float> low, mid, midHz, high;
    int untilUpdate = 0;
    bool primed = false;
};

} // namespace mensis
