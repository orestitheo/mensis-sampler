#include "LoopTrack.h"

namespace mensis {

void LoopTrack::prepare(double newSampleRate) {
    sampleRate = newSampleRate;
    for (auto* v : {&start, &length, &gain, &pan, &pitchRatio, &grainMix}) v->reset(sampleRate, 0.05); // 50 ms glide, no zipper noise
    cloud.prepare(sampleRate);
    primed = false;
}

void LoopTrack::render(const Sample& sample, const LoopParams& p, double ppq, double bpm,
                       juce::AudioBuffer<float>& out, int startSample, int numSamples) {
    const float targetGain = p.on ? juce::Decibels::decibelsToGain(p.levelDb, -60.0f) : 0.0f;
    if (!primed) {
        start.setCurrentAndTargetValue(p.start);
        length.setCurrentAndTargetValue(p.length);
        gain.setCurrentAndTargetValue(targetGain);
        pan.setCurrentAndTargetValue(p.pan);
        pitchRatio.setCurrentAndTargetValue(std::exp2(p.pitch / 12.0f));
        grainMix.setCurrentAndTargetValue(p.grains.mix);
        primed = true;
    }
    start.setTargetValue(p.start);
    length.setTargetValue(p.length);
    gain.setTargetValue(targetGain);
    pan.setTargetValue(p.pan);
    pitchRatio.setTargetValue(std::exp2(p.pitch / 12.0f));
    grainMix.setTargetValue(p.grains.mix);

    const int n = sample.audio.getNumSamples();
    const float* srcL = sample.audio.getReadPointer(0);
    const float* srcR = sample.audio.getReadPointer(sample.audio.getNumChannels() > 1 ? 1 : 0);
    float* outL = out.getWritePointer(0, startSample);
    float* outR = out.getWritePointer(1, startSample);

    const double beatsPerSample = bpm / (60.0 * sampleRate);
    const double loopBeats = p.bars * 4.0; // shortcut: assumes 4/4; read the host time signature if odd meters matter
    const double loopSamples = loopBeats / beatsPerSample;
    const double fadeSamples = 0.005 * sampleRate; // 5 ms fade at the loop seam hides the jump (no click)
    // Around Chaos jumps: 5 ms, but only 1 ms on sliced tracks so hits keep their attack.
    const double fadeBeats = (p.slice ? 0.001 : 0.005) * sampleRate * beatsPerSample;

    // Hits inside the loop region, as loop phase. Phase 0 (the region start) always counts as one, so every
    // moment of the loop belongs to a slice. Reverse plays grain warp, without snapping.
    numSlices = 0;
    if (p.slice && !p.reverse) {
        const double rs = static_cast<double>(start.getCurrentValue()) * (n - 1);
        const double rl = std::max(1.0, static_cast<double>(length.getCurrentValue()) * (n - 1 - rs));
        auto add = [&](double pos) {
            if (numSlices == kMaxSlices) return;
            slicePos[numSlices] = pos;
            slicePhase[numSlices] = (pos - rs) / rl;
            sliceBeats[numSlices] = slicePhase[numSlices] * loopBeats;
            ++numSlices;
        };
        add(rs);
        for (int onset : sample.slices)
            if (onset > rs + 0.002 * sample.sampleRate && onset < rs + rl) add(onset); // 2 ms: same as the start
    }
    LoopParams lp = p; // chaos sees this block's hits
    lp.chaos.slices = p.slice && numSlices > 1 ? std::span<const double>(sliceBeats.data(), numSlices)
                                               : std::span<const double>{};
    const bool beatsMode = p.slice && p.warp && numSlices > 1;
    const double secondsPerPhase = loopBeats * 60.0 / bpm;

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
        const ChaosRead chaos = chaosAt(lp.chaos, loopBeats, beats);
        const double phase = loopPhase(chaos.beats);
        const double regionStart = static_cast<double>(start.getNextValue()) * (n - 1);
        const double regionLen = std::max(1.0, static_cast<double>(length.getNextValue()) * (n - 1 - regionStart));
        auto mapPos = [&](double ph) { return regionStart + (p.reverse ? 1.0 - ph : ph) * regionLen; };
        const double pos = mapPos(phase);

        float l = 0.0f, r = 0.0f;
        const float level = gain.getNextValue();
        float g = level * chaos.gain * static_cast<float>(std::min(1.0, chaos.edgeBeats / fadeBeats));
        if (!p.warp) {
            l = read(srcL, pos);
            r = read(srcR, pos);
            const double samplesFromSeam = std::min(phase, 1.0 - phase) * loopSamples;
            g *= static_cast<float>(std::min(1.0, samplesFromSeam / fadeSamples));
        } else if (beatsMode) {
            // Beats-style: the hit whose time it is plays from its start at its own speed (times Pitch).
            // It fades in over 1 ms; the hit before it fades out over 5 ms, so the switch doesn't click.
            const double filePerPhase = secondsPerPhase * sample.sampleRate * pitchRatio.getNextValue();
            const auto idx = static_cast<size_t>(std::upper_bound(slicePhase.begin(), slicePhase.begin() + static_cast<std::ptrdiff_t>(numSlices), phase)
                                                 - slicePhase.begin()) - 1; // slicePhase[0] == 0, so idx >= 0
            auto voice = [&](size_t s, double sincePhase, double amp) {
                const double end = s + 1 < numSlices ? slicePos[s + 1] : regionStart + regionLen;
                auto play = [&](double at, double a) {
                    const double gate = std::clamp((end - at) / srcFade, 0.0, 1.0); // stop before the next hit's audio
                    if (gate <= 0.0 || at < 0.0) return;
                    l += static_cast<float>(a * gate) * read(srcL, at);
                    r += static_cast<float>(a * gate) * read(srcR, at);
                };
                const double tau = sincePhase * secondsPerPhase;                                  // seconds into the slot
                const double slot = ((s + 1 < numSlices ? slicePhase[s + 1] : 1.0) - slicePhase[s]) * secondsPerPhase;
                const double speed = filePerPhase / secondsPerPhase;                              // file samples per second
                const double fill = (end - slicePos[s]) / std::max(1e-6, slot); // speed that spreads the slice over its slot
                constexpr double hopSec = 0.03;
                const double lastStart = end - 2.0 * hopSec * speed; // a grain starting later would run past the slice
                if (fill >= speed || lastStart <= slicePos[s]) { // fills its slot, or too short to stretch: play straight
                    play(slicePos[s] + tau * speed, amp);
                    return;
                }
                // Slot is longer than the hit: stretch it with grains so it fills the slot without going quiet.
                // Grain j starts every 30 ms at the slowly moving spot slicePos + j*hop*fill and plays at normal
                // speed (pitch kept). Grain 0 starts at full level, so the attack stays crisp. The last grains
                // start early enough to fit inside the slice, so the end of the slot doesn't fade.
                const double k = std::floor(tau / hopSec);
                for (double j : {k, k - 1.0}) {
                    if (j < 0.0) continue;
                    const double age = tau - j * hopSec; // 0 .. 2 hops
                    const double sn = std::sin(juce::MathConstants<double>::pi * age / (2.0 * hopSec));
                    const double w = j == 0.0 && age < hopSec ? 1.0 : sn * sn; // sin^2 windows sum to exactly 1
                    play(std::min(slicePos[s] + j * hopSec * fill, lastStart) + age * speed, amp * w);
                }
            };
            const double since = (phase - slicePhase[idx]) * secondsPerPhase; // seconds since this hit's time
            voice(idx, phase - slicePhase[idx], std::min(1.0, since / 0.001));
            if (since < 0.005) {
                const size_t prev = idx > 0 ? idx - 1 : numSlices - 1; // before the first hit: the loop's last one
                voice(prev, phase - slicePhase[prev] + (idx == 0 ? 1.0 : 0.0), 1.0 - since / 0.005);
            }
        } else {
            const double t = beats / beatsPerSample; // output time in samples since host beat 0
            const double k = std::floor(t / hop);
            const double speed = srcSpeed * pitchRatio.getNextValue(); // grains read faster/slower = higher/lower
            for (double grain : {k, k - 1.0}) {
                const double age = t - grain * hop; // 0 .. 2 * hop
                const double w = std::sin(juce::MathConstants<double>::pi * age / (2.0 * hop));
                const ChaosRead gc = chaosAt(lp.chaos, loopBeats, grain * hop * beatsPerSample);
                double rel = mapPos(loopPhase(gc.beats)) + dir * gc.dir * age * speed - regionStart;
                rel -= std::floor(rel / regionLen) * regionLen; // wrap inside the region
                const double edge = std::min(1.0, std::min(rel, regionLen - rel) / srcFade);
                const auto amp = static_cast<float>(w * w * edge); // the two sin^2 windows sum to exactly 1
                l += amp * read(srcL, regionStart + rel);
                r += amp * read(srcR, regionStart + rel);
            }
        }
        l *= g;
        r *= g;
        // Grain cloud: crossfade from the loop to the cloud. Chaos dips/dropouts only touch the loop.
        if (const float m = grainMix.getNextValue(); m > 0.0f) {
            const auto [wl, wr] = cloud.next(sample, p.grains, beats / beatsPerSample, pos, pitchRatio.getCurrentValue());
            l = l * (1.0f - m) + wl * level * m;
            r = r * (1.0f - m) + wr * level * m;
        }
        const float b = pan.getNextValue();
        outL[i] += l * std::min(1.0f, 1.0f - b);
        outR[i] += r * std::min(1.0f, 1.0f + b);
        lastPos = static_cast<float>(pos / std::max(1, n - 1));
    }
}

} // namespace mensis
