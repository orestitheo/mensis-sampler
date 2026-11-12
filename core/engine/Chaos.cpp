#include "Chaos.h"
#include "Hash.h"
#include <algorithm>
#include <cmath>

namespace mensis {

namespace {

enum class Kind { None, Jump, Reverse, Stutter, Dropout };

struct Event {
    Kind kind = Kind::None;
    double start = 0.0, len = 0.0, value = 0.0; // value: a 0..1 random number each kind uses its own way
};

Event rawEventAt(const ChaosParams& p, int64_t step) {
    const double g = p.gridBeats;
    const uint32_t seed = static_cast<double>(step) * g >= p.switchBeats ? p.nextSeed : p.seed;
    Rng r{mix(mix((static_cast<uint64_t>(seed) << 32) | p.stream) ^ static_cast<uint64_t>(step))};

    // Draw everything up front in a fixed order, so raising Chaos keeps the events you already hear.
    const double coin = r.next(), jitter = r.next(), lenPick = r.next(), kindPick = r.next(), value = r.next();
    const double c = p.amount;
    if (coin >= 0.6 * c) return {};

    // Jumps come first; the other kinds fade in as Chaos rises.
    const double w[] = {1.0, std::max(0.0, c - 0.15), std::max(0.0, c - 0.3), 0.5 * std::max(0.0, c - 0.45)};
    const Kind kinds[] = {Kind::Jump, Kind::Reverse, Kind::Stutter, Kind::Dropout};
    double total = 0.0;
    for (double x : w) total += x;
    double pick = kindPick * total;
    Kind kind = Kind::Jump;
    for (size_t i = 0; i < 4; ++i) {
        if (pick < w[i]) { kind = kinds[i]; break; }
        pick -= w[i];
    }
    const double steps = lenPick < 0.5 ? 1.0 : lenPick < 0.85 ? 2.0 : 4.0;
    const double len = kind == Kind::Dropout ? std::min(steps * g, 0.5) : steps * g; // silences: 1/8 note max
    return {kind, (static_cast<double>(step) + jitter * p.looseness) * g, len, value};
}

// Like rawEventAt, but a Dropout that would follow another one within an 1/8 note becomes a Jump,
// so silences never chain into a long pause.
Event eventAt(const ChaosParams& p, int64_t step) {
    Event e = rawEventAt(p, step);
    if (e.kind != Kind::Dropout) return e;
    const auto lookBack = static_cast<int64_t>(std::ceil(1.0 / p.gridBeats)) + 1; // dropouts are <= 0.5 beat
    for (int64_t s = step - 1; s >= step - lookBack; --s) {
        const Event prev = rawEventAt(p, s);
        if (prev.kind == Kind::Dropout && prev.start + prev.len > e.start - 0.5) {
            e.kind = Kind::Jump;
            break;
        }
    }
    return e;
}

// The hit at or before a moment (host beats), as host beats: back up from `at` to that hit.
double hitAtOrBefore(const ChaosParams& p, double loopBeats, double at) {
    const double inLoop = at - std::floor(at / loopBeats) * loopBeats;
    const auto it = std::upper_bound(p.slices.begin(), p.slices.end(), inLoop);
    return at - (it == p.slices.begin() ? inLoop + loopBeats - p.slices.back() : inLoop - *(it - 1));
}

// BEAT mode: each step either plays as recorded, gets swapped for another hit (one step), or rolls
// (repeats a hit 2 or 4 times per step; 2-step rolls go twice as fast in the second step, like a fill).
enum class BeatKind { None, Swap, Roll };
struct BeatEvent {
    BeatKind kind = BeatKind::None;
    double start = 0.0, len = 0.0, value = 0.0, source = 0.0;
};

BeatEvent beatEventAt(const ChaosParams& p, int64_t step) {
    const double g = p.gridBeats;
    const uint32_t seed = static_cast<double>(step) * g >= p.switchBeats ? p.nextSeed : p.seed;
    Rng r{mix(mix((static_cast<uint64_t>(seed) << 32) | p.stream) ^ static_cast<uint64_t>(step) ^ 0xbea7bea7ULL)};
    const double coin = r.next(), kindPick = r.next(), lenPick = r.next(), value = r.next(), source = r.next();
    const double c = p.amount;
    if (coin >= 0.7 * c) return {};
    const double rollWeight = 1.5 * std::max(0.0, c - 0.2); // rolls join in from ~20% Chaos
    const bool roll = kindPick * (1.0 + rollWeight) >= 1.0;
    const double steps = roll && lenPick >= 0.7 ? 2.0 : 1.0;
    return {roll ? BeatKind::Roll : BeatKind::Swap, static_cast<double>(step) * g, steps * g, value, source};
}

ChaosRead beatAt(const ChaosParams& p, double loopBeats, double beats) {
    ChaosRead out{beats};
    const double g = p.gridBeats;
    const auto step = static_cast<int64_t>(std::floor(beats / g));
    BeatEvent current;
    for (int64_t s = step + 1; s >= step - 1; --s) { // events last 1-2 steps; +1 for the fade before the next
        const BeatEvent e = beatEventAt(p, s);
        if (e.kind == BeatKind::None) continue;
        out.edgeBeats = std::min({out.edgeBeats, std::abs(beats - e.start), std::abs(beats - e.start - e.len)});
        if (current.kind == BeatKind::None && e.start <= beats && beats < e.start + e.len) current = e;
    }
    const auto n = p.slices.size();
    const double t = beats - current.start;
    if (current.kind == BeatKind::Swap) { // another hit of the loop, for this one step
        out.beats = p.slices[std::min(n - 1, static_cast<size_t>(current.value * static_cast<double>(n)))] + t;
    } else if (current.kind == BeatKind::Roll) {
        // 2 or 4 repeats per step (1/32 or 1/64 at a 1/16 grid); faster turns into a buzz, not a roll.
        const double div = current.value < 0.6 ? 2.0 : 4.0;
        const bool second = t >= g; // the second step of a 2-step roll goes twice as fast
        const double period = g / (second ? 2.0 * div : div);
        const double inRep = std::fmod(second ? t - g : t, period);
        // Roll the hit that belongs here, or (sometimes) another hit of the loop.
        const double from = current.source < 0.3
            ? p.slices[std::min(n - 1, static_cast<size_t>(current.source / 0.3 * static_cast<double>(n)))]
            : hitAtOrBefore(p, loopBeats, current.start);
        out.beats = from + inRep;
        out.edgeBeats = std::min({out.edgeBeats, inRep, period - inRep});
    }
    return out;
}

} // namespace

ChaosRead chaosAt(const ChaosParams& p, double loopBeats, double beats) {
    ChaosRead out{beats};
    if (p.amount <= 0.0f || p.gridBeats <= 0.0) return out;
    if (p.beat && !p.slices.empty()) return beatAt(p, loopBeats, beats);

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
        case Kind::Jump: { // play on from another step of the loop, or from a hit
            if (!p.slices.empty()) {
                const auto i = std::min(p.slices.size() - 1, static_cast<size_t>(current.value * static_cast<double>(p.slices.size())));
                out.beats = p.slices[i] + t;
                break;
            }
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
            // With slices, back up to the hit at or before where the stutter starts.
            const double from = p.slices.empty() ? current.start : hitAtOrBefore(p, loopBeats, current.start);
            out.beats = from + inSlice;
            out.edgeBeats = std::min({out.edgeBeats, inSlice, slice - inSlice});
            break;
        }
        case Kind::Dropout: out.gain = 0.0f; break;
    }
    return out;
}

} // namespace mensis
