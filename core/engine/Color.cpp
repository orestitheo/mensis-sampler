#include "Color.h"
#include <cmath>

namespace mensis {

namespace {

// The shaper: warm = tanh, fuzz = hard clip of a harder-pushed signal. f and its antiderivative F.
float fWarm(float x) { return std::tanh(x); }
double FWarm(double x) { // log(cosh(x)), written so it can't overflow
    const double a = std::abs(x);
    return a + std::log1p(std::exp(-2.0 * a)) - std::log(2.0);
}
float fFuzz(float x) { return std::clamp(x, -1.0f, 1.0f); }
double FFuzz(double x) { return std::abs(x) <= 1.0 ? 0.5 * x * x : std::abs(x) - 0.5; }

bool isNeutral(const ColorParams& p, double sampleRate) {
    return p.drive <= 0.0f && p.bits >= 16.0f && p.rateHz >= sampleRate && p.squash <= 0.0f && p.tilt == 0.0f
           && p.outDb == 0.0f;
}

} // namespace

float Color::shape(float x, float prev, float character) {
    // First-order antiderivative anti-aliasing: the average of f over [prev, x] instead of f(x).
    // It acts like a gentle low-pass on what the clipper adds, so harmonics above Nyquist fold back far less.
    const double d = static_cast<double>(x) - prev;
    const auto c = static_cast<double>(std::clamp(character, 0.0f, 1.0f));
    if (std::abs(d) < 1e-5) { // too close to divide: use f at the midpoint
        const float m = 0.5f * (x + prev);
        return static_cast<float>((1.0 - c) * fWarm(m) + c * fFuzz(m));
    }
    const double warm = (FWarm(x) - FWarm(prev)) / d, fuzz = (FFuzz(x) - FFuzz(prev)) / d;
    return static_cast<float>((1.0 - c) * warm + c * fuzz);
}

void Color::prepare(double newSampleRate) {
    sampleRate = newSampleRate;
    primed = false;
    prevIn = {};
    held = {};
    tiltLow = {};
    holdPhase = 1.0;
    envelope = 0.0f;
    for (auto* v : {&drive, &tilt, &outGain, &mix}) v->reset(sampleRate, 0.03);
}

void Color::process(float& l, float& r, const ColorParams& p) {
    const float out = juce::Decibels::decibelsToGain(p.outDb);
    if (!primed) {
        drive.setCurrentAndTargetValue(p.drive);
        tilt.setCurrentAndTargetValue(p.tilt);
        outGain.setCurrentAndTargetValue(out);
        mix.setCurrentAndTargetValue(p.mix);
        primed = true;
    }
    drive.setTargetValue(p.drive);
    tilt.setTargetValue(p.tilt);
    outGain.setTargetValue(out);
    mix.setTargetValue(p.mix);
    const bool settled = !drive.isSmoothing() && !tilt.isSmoothing() && !outGain.isSmoothing() && !mix.isSmoothing();
    if (settled && isNeutral(p, sampleRate)) { // everything off: bypass, bit-identical
        prevIn = {l, r};
        envelope = 0.0f;
        return;
    }

    const float dr = drive.getNextValue(), tl = tilt.getNextValue(), og = outGain.getNextValue(), mx = mix.getNextValue();
    const std::array<float, 2> dry{l, r};
    std::array<float, 2> x{l, r};

    // Drive: up to 36 dB into the shaper (fuzz is pushed 2x harder). Level compensation: a -10 dBFS signal
    // comes out at the same level at any drive; quieter parts come up, as saturation does.
    if (dr > 0.0f) {
        const float c = std::clamp(p.character, 0.0f, 1.0f);
        const float pre = juce::Decibels::decibelsToGain(36.0f * dr) * (1.0f + c);
        constexpr float ref = 0.316f;
        const float makeUp = ref / ((1.0f - c) * std::tanh(ref * pre) + c * std::min(1.0f, ref * pre));
        for (size_t ch = 0; ch < 2; ++ch) {
            const float in = x[ch] * pre;
            x[ch] = shape(in, prevIn[ch], c) * makeUp;
            prevIn[ch] = in;
        }
    } else {
        prevIn = {};
    }

    // Bits: round to 2^(bits-1) steps per side. Continuous, so the knob sweeps smoothly.
    if (p.bits < 16.0f) {
        const float steps = std::exp2(std::max(1.0f, p.bits) - 1.0f);
        for (auto& v : x) v = std::round(v * steps) / steps;
    }

    // Rate: sample-and-hold at rateHz.
    if (p.rateHz < sampleRate) {
        holdPhase += std::max(1.0, static_cast<double>(p.rateHz)) / sampleRate;
        if (holdPhase >= 1.0) {
            holdPhase -= std::floor(holdPhase);
            held = x;
        }
        x = held;
    } else {
        holdPhase = 1.0;
    }

    // Squash: threshold -6..-36 dB, ratio 1..10, make-up so a -10 dBFS signal keeps its level. Instant attack,
    // so hits get caught (a squash, not a punch); 50 ms release, so tails come back up.
    if (p.squash > 0.0f) {
        const float level = std::max(std::abs(x[0]), std::abs(x[1]));
        if (level > envelope) envelope = level; // instant attack: no hit slips through
        else envelope += (level - envelope) * (1.0f - std::exp(-1.0f / (0.05f * static_cast<float>(sampleRate))));
        const float threshold = -6.0f - 30.0f * p.squash, ratio = 1.0f + 9.0f * p.squash;
        const float over = juce::Decibels::gainToDecibels(envelope, -100.0f) - threshold;
        const float reduce = over > 0.0f ? over * (1.0f - 1.0f / ratio) : 0.0f;
        const float makeUp = std::max(0.0f, -10.0f - threshold) * (1.0f - 1.0f / ratio); // -10 dBFS stays put
        const float g = juce::Decibels::decibelsToGain(makeUp - reduce);
        for (auto& v : x) v *= g;
    } else {
        envelope = 0.0f;
    }

    // Tilt: split at 800 Hz, raise one side and lower the other by up to 6 dB.
    if (tl != 0.0f) {
        const auto a = static_cast<float>(1.0 - std::exp(-juce::MathConstants<double>::twoPi * 800.0 / sampleRate));
        const float lowGain = juce::Decibels::decibelsToGain(-6.0f * tl), highGain = juce::Decibels::decibelsToGain(6.0f * tl);
        for (size_t ch = 0; ch < 2; ++ch) {
            tiltLow[ch] += (x[ch] - tiltLow[ch]) * a;
            x[ch] = tiltLow[ch] * lowGain + (x[ch] - tiltLow[ch]) * highGain;
        }
    } else {
        tiltLow = x; // stay in step, so turning tilt on doesn't click
    }

    l = x[0] * og * mx + dry[0] * (1.0f - mx); // at 100% wet this is exactly the processed signal
    r = x[1] * og * mx + dry[1] * (1.0f - mx);
}

} // namespace mensis
