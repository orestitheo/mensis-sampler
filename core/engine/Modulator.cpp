#include "Modulator.h"
#include <algorithm>
#include <cmath>
#include "Hash.h"

namespace mensis {

float Modulator::lfo(float shape, double phase) {
    constexpr double pi = 3.14159265358979323846;
    phase -= std::floor(phase);
    const double sine = std::sin(2.0 * pi * phase);
    const double tri = phase < 0.25 ? 4.0 * phase : phase < 0.75 ? 2.0 - 4.0 * phase : 4.0 * phase - 4.0;
    const double saw = phase < 0.5 ? 2.0 * phase : 2.0 * phase - 2.0; // ramps up, starts at 0 like the others
    const double square = phase < 0.5 ? 1.0 : -1.0;
    // SHAPE sweeps sine -> triangle -> saw -> square, crossfading neighbours.
    const double s = std::clamp(static_cast<double>(shape), 0.0, 1.0) * 3.0;
    const double waves[4] = {sine, tri, saw, square};
    const int i = std::min(2, static_cast<int>(s));
    const double f = s - i;
    return static_cast<float>(waves[i] * (1.0 - f) + waves[i + 1] * f);
}

float Modulator::random(const ModParams& p, double beats) {
    const double rate = std::max(1e-3, p.rateBeats);
    const double pos = beats / rate;
    const auto step = static_cast<int64_t>(std::floor(pos));
    auto valueAt = [&](int64_t k) {
        Rng r{mix(mix((static_cast<uint64_t>(p.seed) << 32) | (p.slot + 0x51u)) ^ static_cast<uint64_t>(k))};
        return static_cast<float>(r.next() * 2.0 - 1.0);
    };
    // SHAPE 0 = hold each value (steps), 1 = glide all the way to the next one (cosine).
    const auto t = static_cast<float>(pos - static_cast<double>(step));
    const float glideStart = 1.0f - std::clamp(p.shape, 0.0f, 1.0f);
    if (t <= glideStart) return valueAt(step);
    const float u = (t - glideStart) / std::max(1e-6f, 1.0f - glideStart);
    const float w = 0.5f - 0.5f * std::cos(3.14159265f * u);
    return valueAt(step) + (valueAt(step + 1) - valueAt(step)) * w;
}

float Modulator::next(const ModParams& p, double beats, float level, double seconds, double bpm) {
    switch (p.type) {
        case ModType::Lfo: return lfo(p.shape, beats / std::max(1e-3, p.rateBeats));
        case ModType::Random: return random(p, beats);
        case ModType::Env: {
            // Rises fast (5 ms) with the loudness, falls over RATE. SHAPE = sensitivity (x1 .. x10).
            const float in = std::clamp(level * (1.0f + 9.0f * p.shape), 0.0f, 1.0f);
            const double release = std::max(0.01, p.rateBeats * 60.0 / std::max(1.0, bpm));
            const double tc = in > env ? 0.005 : release / 6.91; // release = time to fall 60 dB
            env += (in - env) * static_cast<float>(1.0 - std::exp(-seconds / tc));
            return env;
        }
    }
    return 0.0f;
}

} // namespace mensis
