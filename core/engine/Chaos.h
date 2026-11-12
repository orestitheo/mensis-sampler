#pragma once
#include <cstdint>
#include <span>

namespace mensis {

struct ChaosParams {
    float amount = 0.0f;      // 0 = clean, 1 = wild
    double gridBeats = 0.25;  // step size; 0.25 beats = 1/16 note
    float looseness = 0.0f;   // 0 = events start exactly on a step, 1 = anywhere inside it
    uint32_t stream = 0;      // track number, so each track glitches differently with the same seed
    uint32_t seed = 0;        // used for steps before switchBeats
    uint32_t nextSeed = 0;    // used for steps from switchBeats on (Re-roll lands on the next bar)
    double switchBeats = -1e300;
    std::span<const double> slices; // hit positions in loop beats (0..loopBeats), ascending; empty = no snapping
};

// What Chaos does to the read position at one moment.
struct ChaosRead {
    double beats;           // where to read, in host beats; the loop maps this to a spot in the sample
    double dir = 1.0;       // -1 while a Reverse event plays
    float gain = 1.0f;      // 0 during a Dropout
    double edgeBeats = 1e9; // distance to the nearest jump in the output, so the caller can fade around it
};

// With slices, Jump lands on a hit and Stutter repeats from the hit before it.
// Pure function of its inputs: same params + same beats = same answer. Real-time safe.
ChaosRead chaosAt(const ChaosParams& p, double loopBeats, double beats);

} // namespace mensis
