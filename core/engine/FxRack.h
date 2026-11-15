#pragma once
#include "Color.h"
#include "Grains.h"
#include "Resonator.h"
#include "Space.h"

namespace mensis {

struct FxParams {
    GrainParams grains;   // mix 0 = off
    ResonatorParams res;  // mix 0 = off
    FilterParams filter;  // defaults = open
    ColorParams color;    // defaults = neutral
    float send = 0.0f;    // how much goes into Space (0..1); Space's output is added on top
    SpaceParams space;
};

// The Mensis effect rack for live audio (Mensis FX): Grains -> Resonator -> Filter -> Color, plus Space.
// The grain cloud reads from a recording of the last few seconds of input.
// All memory is allocated in prepare(); process() is real-time safe.
class FxRack {
public:
    void prepare(double sampleRate, int maxBlock);
    // In place: l/r hold the input on the way in and the result on the way out.
    void process(float* l, float* r, int numSamples, const FxParams& p, double bpm);

private:
    double sampleRate = 48000.0;
    // Input history. Every sample is written at i, i+N and i+2N, so grains read the middle copy and can run
    // up to N samples either way without hitting the end of the buffer.
    Sample history;
    int ringLen = 0, writePos = 0;
    int64_t clock = 0; // samples since prepare: drives grain timing
    GrainCloud cloud;
    Resonator resonator;
    MorphFilter filter;
    Color color;
    Space space;
    juce::AudioBuffer<float> sendBuf;
    juce::SmoothedValue<float> grainMix, send;
};

} // namespace mensis
