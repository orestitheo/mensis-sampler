#pragma once
#include "Chaos.h"
#include "Color.h"
#include "Eq.h"
#include "Grains.h"
#include "Resonator.h"
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
    float pitch = 0.0f;    // semitones; Warp only (in tape mode the speed sets the pitch)
    bool slice = false;    // Chaos snaps to the sample's hits; with Warp, hits play Beats-style
    bool beat = false;     // with slice: BEAT mode Chaos (hits shuffled + rolls, always on the grid)
    double swing = 0.0;    // 0 = straight .. 0.5 (= 75%): every 2nd grid step comes later. Warp only.
    GrainParams grains;    // mix 0 = off
    bool on = true;        // off = fades out over 50 ms (track mute)
    ResonatorParams res;   // mix 0 = off
    FilterParams filter;   // defaults = open (bypassed)
    ColorParams color;     // defaults = neutral (bypassed)
    EqParams eq;           // defaults = flat (bypassed); last in the track
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
    const GrainCloud& grains() const { return cloud; }

private:
    double sampleRate = 48000.0;
    float lastPos = 0.0f;
    bool primed = false; // first render jumps straight to the param values instead of gliding from 0
    juce::SmoothedValue<float> start, length, gain, pan, pitchRatio, grainMix;
    GrainCloud cloud;
    Resonator resonator;
    MorphFilter filter;
    Color color;
    Eq eq;

    // The hits inside the loop region, rebuilt each block (fixed size: no allocation on the audio thread).
    // shortcut: max 512 hits per loop region; later ones are ignored
    static constexpr size_t kMaxSlices = 512;
    std::array<double, kMaxSlices> slicePhase{}, slicePos{}, sliceBeats{}; // 0..1 of the loop, file sample, loop beats
    size_t numSlices = 0;
};

} // namespace mensis
