#include "Eq.h"
#include <cmath>

namespace mensis {

namespace {
constexpr double kLowHz = 150.0, kHighHz = 6000.0, kShelfQ = 0.707, kBellQ = 0.9;
}

void Eq::prepare(double newSampleRate) {
    sampleRate = newSampleRate;
    bands = {};
    untilUpdate = 0;
    primed = false;
    for (auto* v : {&low, &mid, &midHz, &high}) v->reset(sampleRate, 0.03);
}

void Eq::updateCoefficients(float lowDb, float midDb, float midFreq, float highDb) {
    auto set = [&](Band& b, double g, double k, double m0, double m1, double m2) {
        b.a1 = static_cast<float>(1.0 / (1.0 + g * (g + k)));
        b.a2 = static_cast<float>(g) * b.a1;
        b.a3 = static_cast<float>(g) * b.a2;
        b.m0 = static_cast<float>(m0);
        b.m1 = static_cast<float>(m1);
        b.m2 = static_cast<float>(m2);
    };
    auto tanAt = [&](double hz) { return std::tan(juce::MathConstants<double>::pi * std::min(hz, 0.45 * sampleRate) / sampleRate); };
    auto amp = [](float db) { return std::pow(10.0, db / 40.0); }; // sqrt of the gain
    double a = amp(lowDb);
    set(bands[0], tanAt(kLowHz) / std::sqrt(a), 1.0 / kShelfQ, 1.0, (a - 1.0) / kShelfQ, a * a - 1.0);
    a = amp(midDb);
    set(bands[1], tanAt(midFreq), 1.0 / (kBellQ * a), 1.0, (a * a - 1.0) / (kBellQ * a), 0.0);
    a = amp(highDb);
    set(bands[2], tanAt(kHighHz) * std::sqrt(a), 1.0 / kShelfQ, a * a, (1.0 - a) * a / kShelfQ, 1.0 - a * a);
}

void Eq::process(float& l, float& r, const EqParams& p) {
    const float lowT = std::clamp(p.low, -12.0f, 12.0f), midT = std::clamp(p.mid, -12.0f, 12.0f);
    const float hzT = std::clamp(p.midHz, 200.0f, 8000.0f), highT = std::clamp(p.high, -12.0f, 12.0f);
    if (!primed) {
        low.setCurrentAndTargetValue(lowT);
        mid.setCurrentAndTargetValue(midT);
        midHz.setCurrentAndTargetValue(hzT);
        high.setCurrentAndTargetValue(highT);
        primed = true;
    }
    low.setTargetValue(lowT);
    mid.setTargetValue(midT);
    midHz.setTargetValue(hzT);
    high.setTargetValue(highT);
    if (lowT == 0.0f && midT == 0.0f && highT == 0.0f && !low.isSmoothing() && !mid.isSmoothing() && !high.isSmoothing()) {
        if (untilUpdate != 0) { // flat: bypass, bit-identical, and start clean next time
            for (auto& b : bands) b.ic1 = b.ic2 = {};
            untilUpdate = 0;
        }
        midHz.setCurrentAndTargetValue(hzT);
        return;
    }
    const float lo = low.getNextValue(), mi = mid.getNextValue(), hz = midHz.getNextValue(), hi = high.getNextValue();
    if (--untilUpdate <= 0) {
        updateCoefficients(lo, mi, hz, hi);
        untilUpdate = kUpdateEvery;
    }
    for (size_t ch = 0; ch < 2; ++ch) {
        float& x = ch == 0 ? l : r;
        for (auto& b : bands) {
            const float v3 = x - b.ic2[ch];
            const float v1 = b.a1 * b.ic1[ch] + b.a2 * v3;
            const float v2 = b.ic2[ch] + b.a2 * b.ic1[ch] + b.a3 * v3;
            b.ic1[ch] = 2.0f * v1 - b.ic1[ch];
            b.ic2[ch] = 2.0f * v2 - b.ic2[ch];
            x = b.m0 * x + b.m1 * v1 + b.m2 * v2;
        }
    }
}

} // namespace mensis
