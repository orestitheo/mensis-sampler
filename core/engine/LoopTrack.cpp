#include "LoopTrack.h"

namespace mensis {

void LoopTrack::prepare(double newSampleRate) {
    sampleRate = newSampleRate;
    for (auto* v : {&start, &length, &gain, &pan}) v->reset(sampleRate, 0.05); // 50 ms glide, no zipper noise
    primed = false;
}

void LoopTrack::render(const Sample& sample, const LoopParams& p, double ppq, double bpm,
                       juce::AudioBuffer<float>& out, int startSample, int numSamples) {
    const float targetGain = juce::Decibels::decibelsToGain(p.levelDb, -60.0f);
    if (!primed) {
        start.setCurrentAndTargetValue(p.start);
        length.setCurrentAndTargetValue(p.length);
        gain.setCurrentAndTargetValue(targetGain);
        pan.setCurrentAndTargetValue(p.pan);
        primed = true;
    }
    start.setTargetValue(p.start);
    length.setTargetValue(p.length);
    gain.setTargetValue(targetGain);
    pan.setTargetValue(p.pan);

    const int n = sample.audio.getNumSamples();
    const float* srcL = sample.audio.getReadPointer(0);
    const float* srcR = sample.audio.getReadPointer(sample.audio.getNumChannels() > 1 ? 1 : 0);
    float* outL = out.getWritePointer(0, startSample);
    float* outR = out.getWritePointer(1, startSample);

    const double beatsPerSample = bpm / (60.0 * sampleRate);
    const double loopBeats = p.bars * 4.0; // shortcut: assumes 4/4; read the host time signature if odd meters matter
    const double loopSamples = loopBeats / beatsPerSample;
    const double fadeSamples = 0.005 * sampleRate; // 5 ms fade at the loop seam hides the jump (no click)
    const double fadeBeats = fadeSamples * beatsPerSample;

    // shortcut: linear interpolation aliases when sped up a lot; upgrade to sinc/hermite if it sounds harsh
    auto read = [&](const float* src, double pos) {
        const int i0 = std::clamp(static_cast<int>(pos), 0, n - 1);
        const int i1 = std::min(i0 + 1, n - 1);
        const auto frac = static_cast<float>(pos - i0);
        return src[i0] + frac * (src[i1] - src[i0]);
    };
    auto loopPhase = [&](double beats) {
        const double ph = beats / loopBeats;
        return ph - std::floor(ph); // 0..1 through the loop, also right for negative ppq (count-in)
    };

    // Warp: two grains overlap by half, each reads at the sample's own speed (so pitch stays), and each
    // new grain starts where the tape-style loop would be at that moment (so timing stays locked to Live).
    // Grain times come from the host position too, so there is no state to drift.
    // shortcut: a tempo change moves the grain grid and can click once; smooth bpm if that is audible
    // shortcut: grains are not phase-aligned, so sustained tones wobble at ~33 Hz; upgrade to WSOLA if it sounds phasey
    const double hop = 0.03 * sampleRate;                    // 30 ms hop, 60 ms grains
    const double srcSpeed = sample.sampleRate / sampleRate;  // file samples per output sample
    const double srcFade = 0.005 * sample.sampleRate;        // 5 ms fade at the region edges, in file samples
    const double dir = p.reverse ? -1.0 : 1.0;

    for (int i = 0; i < numSamples; ++i) {
        const double beats = ppq + i * beatsPerSample;
        const ChaosRead chaos = chaosAt(p.chaos, loopBeats, beats);
        const double phase = loopPhase(chaos.beats);
        const double regionStart = static_cast<double>(start.getNextValue()) * (n - 1);
        const double regionLen = std::max(1.0, static_cast<double>(length.getNextValue()) * (n - 1 - regionStart));
        auto mapPos = [&](double ph) { return regionStart + (p.reverse ? 1.0 - ph : ph) * regionLen; };
        const double pos = mapPos(phase);

        float l = 0.0f, r = 0.0f;
        float g = gain.getNextValue() * chaos.gain * static_cast<float>(std::min(1.0, chaos.edgeBeats / fadeBeats));
        if (!p.warp || chaos.tape) {
            l = read(srcL, pos);
            r = read(srcR, pos);
            const double samplesFromSeam = std::min(phase, 1.0 - phase) * loopSamples;
            g *= static_cast<float>(std::min(1.0, samplesFromSeam / fadeSamples));
        } else {
            const double t = beats / beatsPerSample; // output time in samples since host beat 0
            const double k = std::floor(t / hop);
            for (double grain : {k, k - 1.0}) {
                const double age = t - grain * hop; // 0 .. 2 * hop
                const double w = std::sin(juce::MathConstants<double>::pi * age / (2.0 * hop));
                const ChaosRead gc = chaosAt(p.chaos, loopBeats, grain * hop * beatsPerSample);
                double rel = mapPos(loopPhase(gc.beats)) + dir * gc.dir * age * srcSpeed - regionStart;
                rel -= std::floor(rel / regionLen) * regionLen; // wrap inside the region
                const double edge = std::min(1.0, std::min(rel, regionLen - rel) / srcFade);
                const auto amp = static_cast<float>(w * w * edge); // the two sin^2 windows sum to exactly 1
                l += amp * read(srcL, regionStart + rel);
                r += amp * read(srcR, regionStart + rel);
            }
        }
        const float b = pan.getNextValue();
        outL[i] += l * g * std::min(1.0f, 1.0f - b);
        outR[i] += r * g * std::min(1.0f, 1.0f + b);
        lastPos = static_cast<float>(pos / std::max(1, n - 1));
    }
}

} // namespace mensis
