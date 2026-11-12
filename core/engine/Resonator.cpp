#include "Resonator.h"
#include <cmath>

namespace mensis {

namespace {

constexpr std::array<float, 4> kHarmonic{1.0f, 2.0f, 3.0f, 4.0f};       // string
constexpr std::array<float, 4> kBar{1.0f, 2.756f, 5.404f, 8.933f};      // free bar / bell
constexpr std::array<float, 4> kPartialGain{1.0f, 0.6f, 0.4f, 0.3f};
constexpr float kLevel = 0.8f; // overall resonance level: noise in comes out about as loud as it went in

float noteToHz(float note) { return 440.0f * std::exp2((note - 69.0f) / 12.0f); }

} // namespace

void Resonator::prepare(double newSampleRate) {
    sampleRate = newSampleRate;
    modes = {};
    noteHz = {};
    activeModes = 0;
    untilUpdate = 0;
    fresh = true;
    mix.reset(sampleRate, 0.05);
}

void Resonator::updateCoefficients(const ResonatorParams& p) {
    std::array<float, kNotes> target{};
    int count = 0;
    if (p.numMidiNotes > 0) {
        count = std::min(p.numMidiNotes, kNotes);
        for (int i = 0; i < count; ++i) target[static_cast<size_t>(i)] = noteToHz(p.midiNotes[static_cast<size_t>(i)]);
    } else {
        const auto& chord = kChords[static_cast<size_t>(std::clamp(p.chord, 0, static_cast<int>(kChords.size()) - 1))];
        count = chord.count;
        for (int i = 0; i < count; ++i)
            target[static_cast<size_t>(i)] = noteToHz(p.root + static_cast<float>(chord.intervals[static_cast<size_t>(i)]));
    }

    // Glide toward the new notes over ~20 ms (in pitch, not Hz), so retuning doesn't click.
    const auto glide = static_cast<float>(1.0 - std::exp(-kUpdateEvery / (0.02 * sampleRate)));
    for (int i = 0; i < count; ++i) {
        auto& hz = noteHz[static_cast<size_t>(i)];
        const float t = target[static_cast<size_t>(i)];
        hz = hz <= 0.0f || i >= activeModes / kPartials ? t : hz * std::pow(t / hz, glide);
    }
    for (int i = count * kPartials; i < activeModes; ++i) modes[static_cast<size_t>(i)] = {}; // switched off: forget its ring
    activeModes = count * kPartials;

    const float color = std::clamp(p.color, 0.0f, 1.0f);
    const double decay = std::max(0.02f, p.decay);
    for (int i = 0; i < count; ++i)
        for (int k = 0; k < kPartials; ++k) {
            auto& m = modes[static_cast<size_t>(i * kPartials + k)];
            const auto kk = static_cast<size_t>(k);
            const double f = noteHz[static_cast<size_t>(i)] * (kHarmonic[kk] + (kBar[kk] - kHarmonic[kk]) * color);
            if (f >= 0.45 * sampleRate || f < 10.0) { m.gain = 0.0f; continue; } // out of range: silent
            const double t60 = decay / (1.0 + 0.6 * k);                    // higher partials ring shorter
            const double q = f / (6.91 / (juce::MathConstants<double>::pi * t60)); // bandwidth from ring time
            const double w = juce::MathConstants<double>::twoPi * f / sampleRate;
            const double alpha = std::sin(w) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            m.b0 = static_cast<float>(alpha / a0);
            m.a1 = static_cast<float>(-2.0 * std::cos(w) / a0);
            m.a2 = static_cast<float>((1.0 - alpha) / a0);
            // A resonance of bandwidth B passes sqrt(2B/fs) of a noisy input, so lift it by the inverse: the same
            // level for any note and decay. A pure tone right on the resonance gets lifted a lot; tanh below
            // keeps that bounded (and it saturates nicely).
            const double bandwidth = f / q;
            m.gain = static_cast<float>(kPartialGain[kk] * std::sqrt(sampleRate / (2.0 * bandwidth)) * kLevel
                                        / std::sqrt(static_cast<double>(count)));
        }
}

void Resonator::process(float& l, float& r, const ResonatorParams& p) {
    if (fresh) {
        mix.setCurrentAndTargetValue(p.mix);
        fresh = false;
    }
    mix.setTargetValue(p.mix);
    const float m = mix.getNextValue();
    if (m <= 0.0f) { // off: skip the work, and start clean next time
        if (activeModes > 0) {
            modes = {};
            activeModes = 0;
            untilUpdate = 0;
        }
        return;
    }
    if (--untilUpdate <= 0) {
        updateCoefficients(p);
        untilUpdate = kUpdateEvery;
    }

    const std::array<float, 2> in{l, r};
    std::array<float, 2> wet{};
    for (int i = 0; i < activeModes; ++i) {
        auto& md = modes[static_cast<size_t>(i)];
        if (md.gain == 0.0f) continue;
        for (size_t ch = 0; ch < 2; ++ch) {
            const float y = md.b0 * (in[ch] - md.x2[ch]) - md.a1 * md.y1[ch] - md.a2 * md.y2[ch];
            md.x2[ch] = md.x1[ch];
            md.x1[ch] = in[ch];
            md.y2[ch] = md.y1[ch];
            md.y1[ch] = y;
            wet[ch] += y * md.gain;
        }
    }
    l = l * (1.0f - m) + std::tanh(wet[0]) * m;
    r = r * (1.0f - m) + std::tanh(wet[1]) * m;
}

void MorphFilter::prepare(double newSampleRate) {
    sampleRate = newSampleRate;
    ic1 = {};
    ic2 = {};
    cutoff.reset(sampleRate, 0.02);
    reso.reset(sampleRate, 0.02);
    morph.reset(sampleRate, 0.02);
    primed = false;
}

void MorphFilter::process(float& l, float& r, const FilterParams& p) {
    if (!primed) {
        cutoff.setCurrentAndTargetValue(p.cutoff);
        reso.setCurrentAndTargetValue(p.reso);
        morph.setCurrentAndTargetValue(p.morph);
        primed = true;
    }
    cutoff.setTargetValue(std::clamp(p.cutoff, 20.0f, 20000.0f));
    reso.setTargetValue(p.reso);
    morph.setTargetValue(p.morph);
    if (p.isDefault() && !cutoff.isSmoothing() && !reso.isSmoothing() && !morph.isSmoothing()) {
        ic1 = {}; // fully open: bypass, bit-identical
        ic2 = {};
        return;
    }
    const float fc = std::min(cutoff.getNextValue(), static_cast<float>(0.49 * sampleRate));
    const float k = 2.0f - 1.98f * std::clamp(reso.getNextValue(), 0.0f, 1.0f); // 2 = no resonance
    const float mo = std::clamp(morph.getNextValue(), 0.0f, 1.0f);
    const auto g = static_cast<float>(std::tan(juce::MathConstants<double>::pi * fc / sampleRate));
    const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;

    for (size_t ch = 0; ch < 2; ++ch) {
        float& x = ch == 0 ? l : r;
        const float v3 = x - ic2[ch];
        const float v1 = a1 * ic1[ch] + a2 * v3;
        const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = 2.0f * v1 - ic1[ch];
        ic2[ch] = 2.0f * v2 - ic2[ch];
        const float lp = v2, bp = v1 * k, hp = x - k * v1 - v2; // bp * k: unity gain at the cutoff
        x = mo < 0.5f ? lp + (bp - lp) * (mo * 2.0f) : bp + (hp - bp) * ((mo - 0.5f) * 2.0f);
    }
}

} // namespace mensis
