#include "Chaos.h"
#include <algorithm>
#include <cmath>

namespace mensis {

namespace {

uint64_t mix(uint64_t x) { // splitmix64: turns any number into a well-scrambled one
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

struct Rng {
    uint64_t state;
    double next() { state = mix(state); return static_cast<double>(state >> 11) * 0x1.0p-53; } // 0..1
};

enum class Kind { None, Jump, Reverse, Stutter, Dropout, TapeStop };

struct Event {
    Kind kind = Kind::None;
    double start = 0.0, len = 0.0, value = 0.0; // value: a 0..1 random number each kind uses its own way
};

Event eventAt(const ChaosParams& p, int64_t step) {
    const double g = p.gridBeats;
    const uint32_t seed = static_cast<double>(step) * g >= p.switchBeats ? p.nextSeed : p.seed;
    Rng r{mix(mix((static_cast<uint64_t>(seed) << 32) | p.stream) ^ static_cast<uint64_t>(step))};

    // Draw everything up front in a fixed order, so raising Chaos keeps the events you already hear.
    const double coin = r.next(), jitter = r.next(), lenPick = r.next(), kindPick = r.next(), value = r.next();
    const double c = p.amount;
    if (coin >= 0.6 * c) return {};

    // Jumps come first; the other kinds fade in as Chaos rises.
    const double w[] = {1.0, std::max(0.0, c - 0.15), std::max(0.0, c - 0.3),
                        0.5 * std::max(0.0, c - 0.45), 0.6 * std::max(0.0, c - 0.55)};
    const Kind kinds[] = {Kind::Jump, Kind::Reverse, Kind::Stutter, Kind::Dropout, Kind::TapeStop};
    double total = 0.0;
    for (double x : w) total += x;
    double pick = kindPick * total;
    Kind kind = Kind::Jump;
    for (size_t i = 0; i < 5; ++i) {
        if (pick < w[i]) { kind = kinds[i]; break; }
        pick -= w[i];
    }
    const double steps = lenPick < 0.5 ? 1.0 : lenPick < 0.85 ? 2.0 : 4.0;
    return {kind, (static_cast<double>(step) + jitter * p.looseness) * g, steps * g, value};
}

} // namespace

ChaosRead chaosAt(const ChaosParams& p, double loopBeats, double beats) {
    ChaosRead out{beats};
    if (p.amount <= 0.0f || p.gridBeats <= 0.0) return out;

    const double g = p.gridBeats;
    const auto step = static_cast<int64_t>(std::floor(beats / g));
    Event current;
    // Events last at most 4 steps (+1 for looseness): look back that far, newest first. Also look one step
    // ahead so the fade before the next event starts in time.
    for (int64_t s = step + 1; s >= step - 5; --s) {
        const Event e = eventAt(p, s);
        if (e.kind == Kind::None) continue;
        out.edgeBeats = std::min({out.edgeBeats, std::abs(beats - e.start), std::abs(beats - e.start - e.len)});
        if (current.kind == Kind::None && e.start <= beats && beats < e.start + e.len) current = e;
    }

    const double t = beats - current.start; // time since the event started
    switch (current.kind) {
        case Kind::None: break;
        case Kind::Jump: { // play on from another step of the loop
            const double loopSteps = std::max(1.0, std::floor(loopBeats / g));
            out.beats = beats + std::floor(current.value * loopSteps) * g;
            break;
        }
        case Kind::Reverse: // play backwards from where the event started
            out.beats = current.start - t;
            out.dir = -1.0;
            break;
        case Kind::Stutter: { // repeat a slice of 1, 1/2 or 1/4 step
            const double slice = g / (current.value < 0.34 ? 1.0 : current.value < 0.67 ? 2.0 : 4.0);
            const double inSlice = std::fmod(t, slice);
            out.beats = current.start + inSlice;
            out.edgeBeats = std::min({out.edgeBeats, inSlice, slice - inSlice});
            break;
        }
        case Kind::Dropout: out.gain = 0.0f; break;
        case Kind::TapeStop: // speed falls in a straight line from 1 to 0 over the event
            out.beats = current.start + t - t * t / (2.0 * current.len);
            out.tape = true;
            break;
    }
    return out;
}

} // namespace mensis
