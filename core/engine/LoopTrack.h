#pragma once
#include "Chaos.h"
#include "Sample.h"

namespace mensis {

struct LoopParams {
    float start = 0.0f;   // 0..1, where the loop starts in the sample
    float length = 1.0f;  // 0..1 of what's left after start
    double bars = 1.0;    // the loop region is stretched to this many bars; speed (and pitch) follow, like tape
    bool reverse = false;
    float levelDb = -6.0f; // -60 = silent
    float pan = 0.0f;      // -1 left .. +1 right (balance)
    bool warp = false;     // true: keep the original pitch, stretch time with overlapping grains
    ChaosParams chaos;     // amount 0 = plays clean
};

// One looping track. The play position comes straight from the host's beat position,
// so the loop can never drift out of sync with Live.
class LoopTrack {
public:
    void prepare(double sampleRate);

    // Adds this track into out (stereo), samples [startSample, startSample + numSamples).
    // ppq = host beat position at startSample. Real-time safe: no allocation, no locks.
    void render(const Sample& sample, const LoopParams& params, double ppq, double bpm,
                juce::AudioBuffer<float>& out, int startSample, int numSamples);

    // Where the last rendered sample was read, 0..1 of the whole sample (for the playhead in the UI).
    float position() const { return lastPos; }

private:
    double sampleRate = 48000.0;
    float lastPos = 0.0f;
    bool primed = false; // first render jumps straight to the param values instead of gliding from 0
    juce::SmoothedValue<float> start, length, gain, pan;
};

} // namespace mensis
