#include "Grains.h"
#include <algorithm>
#include <cmath>
#include "Hash.h"

namespace mensis {

void GrainCloud::prepare(double newSampleRate) {
    sampleRate = newSampleRate;
    for (auto& g : grains) g.active = false;
    lastSpawned = INT64_MIN;
    frozenPos = -1.0;
}

int GrainCloud::activeGrains() const {
    return static_cast<int>(std::count_if(grains.begin(), grains.end(), [](const Grain& g) { return g.active; }));
}

GrainCloud::View GrainCloud::view(size_t i, int numSamples) const {
    const Grain& g = grains[i];
    if (!g.active) return {};
    const double s = std::sin(juce::MathConstants<double>::pi * (g.age + 0.5) / g.len);
    return {static_cast<float>(g.pos / std::max(1, numSamples - 1)), static_cast<float>(s * s), g.pan, g.step < 0.0};
}

void GrainCloud::spawn(const Sample& sample, const GrainParams& p, uint64_t key, double basePos, double pitchRatio) {
    // All draws in a fixed order, so changing one knob doesn't reshuffle the others.
    Rng r{mix(key ^ 0x5bd1e995ULL)};
    const double offsetPick = r.next(), detunePick = r.next(), reversePick = r.next(), panPick = r.next();

    const double spray = std::min(1.0, static_cast<double>(p.spray) + 0.5 * p.chaos);
    const double offset = (offsetPick * 2.0 - 1.0) * spray * 0.5 * sample.sampleRate;
    const double semis = (detunePick * 2.0 - 1.0) * p.detune;
    const bool backwards = reversePick < p.reverse;
    const auto pan = static_cast<float>((panPick * 2.0 - 1.0) * p.width);

    Grain g;
    g.active = true;
    g.len = std::max(16, static_cast<int>(p.sizeMs * 0.001 * sampleRate));
    g.step = sample.sampleRate / sampleRate * pitchRatio * std::exp2(semis / 12.0) * (backwards ? -1.0 : 1.0);
    // A backwards grain starts at the far end, so it covers the same stretch of sample as a forward one.
    // Keep the whole grain inside the sample, so it never stops abruptly (that would click).
    const double n = sample.audio.getNumSamples() - 1, span = g.len * std::abs(g.step);
    const double lo = backwards ? std::min(span, n) : 0.0, hi = backwards ? n : std::max(0.0, n - span);
    g.pos = std::clamp(basePos + offset + (backwards ? span : 0.0), lo, std::max(lo, hi));
    g.pan = pan;
    g.gainL = std::min(1.0f, 1.0f - pan);
    g.gainR = std::min(1.0f, 1.0f + pan);

    // Free slot, or steal the grain that is closest to finishing (it's the quietest).
    auto* slot = std::find_if(grains.begin(), grains.end(), [](const Grain& x) { return !x.active; });
    if (slot == grains.end())
        slot = std::max_element(grains.begin(), grains.end(), [](const Grain& a, const Grain& b) {
            return static_cast<double>(a.age) / a.len < static_cast<double>(b.age) / b.len;
        });
    *slot = g;
}

std::pair<float, float> GrainCloud::next(const Sample& sample, const GrainParams& p, double hostSample,
                                         double followPos, double pitchRatio) {
    if (!p.freeze || frozenPos < 0.0) frozenPos = followPos; // FREEZE holds the last followed spot

    // Grain k is due at (k + random 0..1) periods of host time.
    const double period = sampleRate / std::max(0.1f, p.density);
    const auto k = static_cast<int64_t>(std::floor(hostSample / period));
    if (lastSpawned == INT64_MIN || k < lastSpawned) lastSpawned = k - 1; // first call, or Live jumped back
    if (k > lastSpawned + 1) lastSpawned = k - 1; // skipped ahead (jump, density change): don't flood
    if (k > lastSpawned) {
        const uint64_t key = mix(mix((static_cast<uint64_t>(p.seed) << 32) | p.stream) ^ static_cast<uint64_t>(k));
        Rng r{key};
        const double due = r.next(), burst = r.next();
        if (hostSample >= (static_cast<double>(k) + due) * period) {
            spawn(sample, p, key, frozenPos, pitchRatio);
            if (burst < 0.3 * p.chaos) { // Chaos: now and then a burst of 3
                spawn(sample, p, key ^ 1, frozenPos, pitchRatio);
                spawn(sample, p, key ^ 2, frozenPos, pitchRatio);
            }
            lastSpawned = k;
        }
    }

    const int n = sample.audio.getNumSamples();
    const float* srcL = sample.audio.getReadPointer(0);
    const float* srcR = sample.audio.getReadPointer(sample.audio.getNumChannels() > 1 ? 1 : 0);
    float l = 0.0f, r = 0.0f;
    for (auto& g : grains) {
        if (!g.active) continue;
        const double s = std::sin(juce::MathConstants<double>::pi * (g.age + 0.5) / g.len);
        const auto w = static_cast<float>(s * s); // sine-squared window: starts and ends at 0, no clicks
        const int i0 = std::clamp(static_cast<int>(g.pos), 0, n - 1), i1 = std::min(i0 + 1, n - 1);
        const auto frac = static_cast<float>(g.pos - i0);
        l += w * g.gainL * (srcL[i0] + frac * (srcL[i1] - srcL[i0]));
        r += w * g.gainR * (srcR[i0] + frac * (srcR[i1] - srcR[i0]));
        g.pos += g.step;
        if (++g.age >= g.len || g.pos < 0.0 || g.pos > n - 1) g.active = false; // ran off the sample: stop
    }
    // Many overlapping grains add up; scale by 1/sqrt(overlap) so a dense cloud isn't much louder.
    // Chaos bursts add 2 grains 30% * chaos of the time, so they count too.
    const double overlap = p.density * (1.0 + 0.6 * p.chaos) * p.sizeMs * 0.001;
    const auto norm = static_cast<float>(1.0 / std::sqrt(std::max(1.0, overlap)));
    return {l * norm, r * norm};
}

} // namespace mensis
