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

    for (int i = 0; i < numSamples; ++i) {
        double phase = (ppq + i * beatsPerSample) / loopBeats;
        phase -= std::floor(phase); // 0..1 through the loop, also right for negative ppq (count-in)

        const double regionStart = static_cast<double>(start.getNextValue()) * (n - 1);
        const double regionLen = std::max(1.0, static_cast<double>(length.getNextValue()) * (n - 1 - regionStart));
        const double pos = regionStart + (p.reverse ? 1.0 - phase : phase) * regionLen;

        // shortcut: linear interpolation aliases when sped up a lot; upgrade to sinc/hermite if it sounds harsh
        const int i0 = std::min(static_cast<int>(pos), n - 1);
        const int i1 = std::min(i0 + 1, n - 1);
        const auto frac = static_cast<float>(pos - i0);
        const float l = srcL[i0] + frac * (srcL[i1] - srcL[i0]);
        const float r = srcR[i0] + frac * (srcR[i1] - srcR[i0]);

        const double samplesFromSeam = std::min(phase, 1.0 - phase) * loopSamples;
        const float g = gain.getNextValue() * static_cast<float>(std::min(1.0, samplesFromSeam / fadeSamples));
        const float b = pan.getNextValue();
        outL[i] += l * g * std::min(1.0f, 1.0f - b);
        outR[i] += r * g * std::min(1.0f, 1.0f + b);
    }
}

} // namespace mensis
