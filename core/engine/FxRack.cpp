#include "FxRack.h"
#include <algorithm>
#include <cmath>

namespace mensis {

void FxRack::prepare(double newSampleRate, int maxBlock) {
    sampleRate = newSampleRate;
    ringLen = static_cast<int>(4.0 * sampleRate); // 4 s: enough for the widest spray + longest, highest grain
    history.audio.setSize(2, 3 * ringLen);
    history.audio.clear();
    history.sampleRate = sampleRate;
    writePos = 0;
    clock = 0;
    cloud.prepare(sampleRate);
    resonator.prepare(sampleRate);
    filter.prepare(sampleRate);
    color.prepare(sampleRate);
    space.prepare(sampleRate);
    sendBuf.setSize(2, std::max(64, maxBlock));
    grainMix.reset(sampleRate, 0.05);
    send.reset(sampleRate, 0.05);
}

void FxRack::process(float* l, float* r, int numSamples, const FxParams& p, double bpm) {
    grainMix.setTargetValue(p.grains.mix);
    send.setTargetValue(p.send);

    // Grains must only read audio that's already recorded. A grain starts up to `spray` away from the
    // followed spot and reads `len * step` samples, so follow that far behind the newest sample.
    const double spray = std::min(1.0, static_cast<double>(p.grains.spray) + 0.5 * p.grains.chaos) * 0.5 * sampleRate;
    const double span = p.grains.sizeMs * 0.001 * sampleRate * std::exp2(p.grains.detune / 12.0);
    const double lag = spray + span + 2.0;

    float* histL = history.audio.getWritePointer(0);
    float* histR = history.audio.getWritePointer(1);
    const int chunk = sendBuf.getNumSamples(); // Space works in blocks; bigger host blocks go in chunks
    for (int off = 0; off < numSamples; off += chunk) {
        const int n = std::min(chunk, numSamples - off);
        float* sl = sendBuf.getWritePointer(0);
        float* sr = sendBuf.getWritePointer(1);
        for (int i = 0; i < n; ++i) {
            float a = l[off + i], b = r[off + i];
            if (!p.grains.freeze) { // FREEZE stops recording, so the held spot keeps its sound
                for (int k = 0; k < 3; ++k) {
                    histL[writePos + k * ringLen] = a;
                    histR[writePos + k * ringLen] = b;
                }
                writePos = (writePos + 1) % ringLen;
            }
            if (const float m = grainMix.getNextValue(); m > 0.0f) {
                double follow = std::fmod(writePos - lag, static_cast<double>(ringLen));
                if (follow < 0.0) follow += ringLen;
                const auto [gl, gr] = cloud.next(history, p.grains, static_cast<double>(clock), follow + ringLen, 1.0);
                a = a * (1.0f - m) + gl * m;
                b = b * (1.0f - m) + gr * m;
            }
            ++clock;
            resonator.process(a, b, p.res);
            filter.process(a, b, p.filter);
            color.process(a, b, p.color);
            l[off + i] = a;
            r[off + i] = b;
            const float g = send.getNextValue();
            sl[i] = a * g;
            sr[i] = b * g;
        }
        space.process(sl, sr, n, p.space, bpm);
        for (int i = 0; i < n; ++i) {
            l[off + i] += sl[i];
            r[off + i] += sr[i];
        }
    }
}

} // namespace mensis
