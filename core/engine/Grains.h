#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include "Sample.h"

namespace mensis {

struct GrainParams {
    float mix = 0.0f;         // 0 = only the loop (grains off), 1 = only the cloud
    float sizeMs = 80.0f;     // grain length
    float density = 20.0f;    // grains per second
    float spray = 0.2f;       // 0..1: random start offset, up to +-0.5 s around the followed spot
    float detune = 0.0f;      // 0..12 semitones of random pitch per grain
    float reverse = 0.0f;     // 0..1 chance a grain plays backwards
    float width = 0.5f;       // 0..1 random pan per grain
    bool freeze = false;      // hold the spot the cloud reads from
    float chaos = 0.0f;       // 0..1: more spray, and sometimes bursts of grains
    uint32_t seed = 0, stream = 0;
};

// A cloud of short grains read from the sample, around a spot that follows the loop.
// Spawn times and random choices come from host time + seed, so the same seed and song position give
// the same cloud. Fixed pool, no allocation: real-time safe.
class GrainCloud {
public:
    void prepare(double sampleRate);

    // One output sample. hostSample = host time in samples (song position), followPos = where the loop is
    // reading in the sample right now (file samples), pitchRatio = the track's pitch.
    // Returns the cloud's stereo output (not yet mixed with the loop).
    std::pair<float, float> next(const Sample& sample, const GrainParams& p, double hostSample, double followPos,
                                 double pitchRatio);

    int activeGrains() const;

    // For drawing: where grain i is (0..1 of the sample), how loud its window is now (0 = not playing),
    // its pan (-1..1) and direction.
    struct View { float x = 0.0f, env = 0.0f, pan = 0.0f; bool backwards = false; };
    View view(size_t i, int numSamples) const;
    float anchor(int numSamples) const { return frozenPos < 0.0 ? -1.0f : static_cast<float>(frozenPos / std::max(1, numSamples - 1)); }

    static constexpr size_t kMaxGrains = 64;

private:
    struct Grain {
        bool active = false;
        double pos = 0.0, step = 0.0; // file samples, per output sample (negative = backwards)
        int age = 0, len = 0;
        float gainL = 0.0f, gainR = 0.0f, pan = 0.0f;
    };
    void spawn(const Sample& sample, const GrainParams& p, uint64_t key, double basePos, double pitchRatio);

    std::array<Grain, kMaxGrains> grains{};
    double sampleRate = 48000.0;
    int64_t lastSpawned = INT64_MIN;
    double frozenPos = -1.0;
};

} // namespace mensis
