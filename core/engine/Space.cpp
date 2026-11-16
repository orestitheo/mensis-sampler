#include "Space.h"
#include <cmath>

namespace mensis {

namespace {

constexpr double kMaxDelaySec = 6.0;
constexpr std::array<double, 8> kLineMs{29.7, 37.1, 41.1, 43.7, 53.3, 59.5, 67.9, 73.1}; // no common factors
constexpr std::array<double, 8> kLfoHz{0.11, 0.17, 0.23, 0.29, 0.37, 0.43, 0.53, 0.61};
constexpr double kModMs = 0.6;     // line-length wobble: smears the metallic ring
constexpr double kMaxScale = 2.0;  // SIZE stretches the lines up to 2x
constexpr std::array<std::array<double, 4>, 2> kDiffMs{{{4.7, 3.6, 12.7, 9.3}, {5.1, 3.9, 11.9, 8.7}}};
constexpr float kDiffGain = 0.6f;

// In-place fast Walsh-Hadamard transform, normalised: mixes every line into every other, energy-preserving.
void hadamard(std::array<float, 8>& v) {
    for (size_t h = 1; h < 8; h *= 2)
        for (size_t i = 0; i < 8; i += 2 * h)
            for (size_t j = i; j < i + h; ++j) {
                const float a = v[j], b = v[j + h];
                v[j] = a + b;
                v[j + h] = a - b;
            }
    for (auto& x : v) x *= 0.35355339f; // 1/sqrt(8)
}

} // namespace

float Space::readAt(const std::vector<float>& buf, int writePos, double delay) {
    const auto n = static_cast<int>(buf.size());
    double p = writePos - delay;
    p -= std::floor(p / n) * n;
    if (p >= n) p = 0.0; // a tiny negative p rounds up to exactly n: one past the end
    const int i0 = static_cast<int>(p), i1 = (i0 + 1) % n;
    const auto frac = static_cast<float>(p - i0);
    return buf[static_cast<size_t>(i0)] + frac * (buf[static_cast<size_t>(i1)] - buf[static_cast<size_t>(i0)]);
}

void Space::prepare(double newSampleRate) {
    sampleRate = newSampleRate;
    for (auto& b : delayBuf) b.assign(static_cast<size_t>(kMaxDelaySec * sampleRate) + 8, 0.0f);
    const auto lineLen = static_cast<size_t>((kLineMs.back() * kMaxScale + 2.0 * kModMs) * 0.001 * sampleRate) + 8;
    for (auto& l : lines) l.assign(lineLen, 0.0f);
    for (size_t ch = 0; ch < 2; ++ch)
        for (size_t d = 0; d < kDiffusers; ++d)
            diffusers[ch][d].assign(static_cast<size_t>(kDiffMs[ch][d] * 0.001 * sampleRate) + 1, 0.0f);
    delayPos = linePos = 0;
    diffPos = {};
    delayLow = {};
    lineLow = {};
    dcLow = {};
    lfoPhase = 0.0;
    delayTime.reset(sampleRate, 0.2);
    for (auto* v : {&feedback, &blend, &tone, &level}) v->reset(sampleRate, 0.05);
    size.reset(sampleRate, 0.5); // slow: line lengths glide
    primed = false;
}

void Space::process(float* l, float* r, int numSamples, const SpaceParams& p, double bpm) {
    juce::ScopedNoDenormals noDenormals;
    const auto maxDelay = static_cast<float>(delayBuf[0].size() - 4);
    const float targetDelay = std::clamp(static_cast<float>(p.timeBeats * 60.0 / std::max(1.0, bpm) * sampleRate), 1.0f, maxDelay);
    if (!primed) {
        delayTime.setCurrentAndTargetValue(targetDelay);
        feedback.setCurrentAndTargetValue(p.feedback);
        blend.setCurrentAndTargetValue(p.blend);
        size.setCurrentAndTargetValue(p.size);
        tone.setCurrentAndTargetValue(p.tone);
        level.setCurrentAndTargetValue(p.level);
        primed = true;
    }
    delayTime.setTargetValue(targetDelay);
    feedback.setTargetValue(std::clamp(p.feedback, 0.0f, 0.95f));
    blend.setTargetValue(p.blend);
    size.setTargetValue(p.size);
    tone.setTargetValue(p.tone);
    level.setTargetValue(p.level);

    const auto delayN = static_cast<int>(delayBuf[0].size()), lineN = static_cast<int>(lines[0].size());
    const double twoPi = juce::MathConstants<double>::twoPi;
    const auto dcA = static_cast<float>(1.0 - std::exp(-twoPi * 30.0 / sampleRate));

    for (int i = 0; i < numSamples; ++i) {
        const float d = delayTime.getNextValue(), fb = feedback.getNextValue(), bl = blend.getNextValue();
        const float sz = size.getNextValue(), tn = tone.getNextValue(), lv = level.getNextValue();
        const std::array<float, 2> in{p.freeze ? 0.0f : l[i], p.freeze ? 0.0f : r[i]};

        // Tone: one low-pass coefficient for both loops, 1 kHz (dark) .. 16 kHz (bright).
        const auto toneA = static_cast<float>(1.0 - std::exp(-twoPi * 1000.0 * std::pow(16.0, tn) / sampleRate));

        // --- Delay: ping-pong, the left echo feeds the right line and back. ---
        const std::array<float, 2> echo{readAt(delayBuf[0], delayPos, d), readAt(delayBuf[1], delayPos, d)};
        for (size_t ch = 0; ch < 2; ++ch) {
            float back = echo[1 - ch];
            if (!p.freeze) { // tone + soft saturation in the loop; frozen = lossless
                delayLow[ch] += (back - delayLow[ch]) * toneA;
                back = std::tanh(delayLow[ch] * fb);
            }
            delayBuf[ch][static_cast<size_t>(delayPos)] = in[ch] + back;
        }
        delayPos = (delayPos + 1) % delayN;

        // --- Reverb: diffuse the input (plus the echoes), then an 8-line feedback network. ---
        std::array<float, 2> diffused{in[0] + 0.5f * echo[0], in[1] + 0.5f * echo[1]};
        for (size_t ch = 0; ch < 2; ++ch) { // 30 Hz high-pass: keeps DC and sub-rumble out of the long tail
            dcLow[ch] += (diffused[ch] - dcLow[ch]) * dcA;
            diffused[ch] -= dcLow[ch];
        }
        if (p.freeze) diffused = {};
        for (size_t ch = 0; ch < 2; ++ch)
            for (size_t k = 0; k < kDiffusers; ++k) { // series all-passes smear the attack into a wash
                auto& buf = diffusers[ch][k];
                auto& pos = diffPos[ch][k];
                const float delayed = buf[static_cast<size_t>(pos)];
                const float v = diffused[ch] + kDiffGain * delayed;
                buf[static_cast<size_t>(pos)] = v;
                diffused[ch] = delayed - kDiffGain * v;
                pos = (pos + 1) % static_cast<int>(buf.size());
            }

        const double scale = 0.5 + 1.5 * sz;              // line lengths x0.5 .. x2
        const double t60 = 0.5 * std::pow(40.0, sz);        // 0.5 s .. 20 s
        std::array<float, 8> y{};
        std::array<double, 8> lenSec{};
        for (size_t k = 0; k < kLines; ++k) {
            lenSec[k] = kLineMs[k] * 0.001 * scale;
            const double wobble = kModMs * 0.001 * sampleRate * (1.0 + std::sin(twoPi * kLfoHz[k] * lfoPhase + static_cast<double>(k)));
            y[k] = readAt(lines[k], linePos, lenSec[k] * sampleRate + wobble);
        }
        lfoPhase += 1.0 / sampleRate;
        if (lfoPhase > 1000.0) lfoPhase -= 1000.0; // keep sin() accurate; the LFO rates make this seamless enough

        std::array<float, 8> mixed{};
        for (size_t k = 0; k < kLines; ++k) {
            if (p.freeze) {
                mixed[k] = y[k];
            } else {
                lineLow[k] += (y[k] - lineLow[k]) * toneA; // damping: highs die first, like a real room
                mixed[k] = lineLow[k] * static_cast<float>(std::pow(10.0, -3.0 * lenSec[k] / t60));
            }
        }
        hadamard(mixed);
        for (size_t k = 0; k < kLines; ++k)
            lines[k][static_cast<size_t>(linePos)] = mixed[k] + 0.5f * diffused[k % 2];
        linePos = (linePos + 1) % lineN;

        float revL = 0.0f, revR = 0.0f;
        for (size_t k = 0; k < kLines; k += 2) {
            revL += y[k];
            revR += y[k + 1];
        }
        revL *= 0.5f;
        revR *= 0.5f;

        l[i] = (echo[0] * (1.0f - bl) + revL * bl) * lv;
        r[i] = (echo[1] * (1.0f - bl) + revR * bl) * lv;
    }
}

} // namespace mensis
