#pragma once
#include <cstdint>

namespace mensis {

enum class ModType { Lfo = 0, Random = 1, Env = 2 };

struct ModParams {
    ModType type = ModType::Lfo;
    double rateBeats = 4.0; // cycle (LFO), step (Random) or release (Env), in beats
    float shape = 0.0f;     // LFO: sine->tri->saw->square. Random: smoothness. Env: sensitivity.
    uint32_t seed = 0, slot = 0;
};

// One modulator. LFO and Random are pure functions of the song position (locked to Live, repeatable);
// Env follows a loudness and keeps a little state. Real-time safe.
class Modulator {
public:
    void reset() { env = 0.0f; }
    // beats = host position; level = loudness feeding Env (peak, 0..1+); seconds = time since last call.
    // Returns -1..1 for LFO/Random, 0..1 for Env.
    float next(const ModParams& p, double beats, float level, double seconds, double bpm);

    static float lfo(float shape, double phase);                                         // phase 0..1
    static float random(const ModParams& p, double beats);

private:
    float env = 0.0f;
};

} // namespace mensis
