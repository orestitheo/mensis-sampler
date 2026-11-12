#include "Slices.h"
#include <algorithm>
#include <cmath>

namespace mensis {

namespace {

float percentile(std::vector<float> v, float q) {
    if (v.empty()) return 0.0f;
    const auto k = static_cast<size_t>(q * static_cast<float>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

} // namespace

SliceInfo findSlices(const juce::AudioBuffer<float>& audio, double sampleRate) {
    SliceInfo info;
    const int n = audio.getNumSamples(), channels = audio.getNumChannels();
    const int hop = std::max(64, static_cast<int>(0.0058 * sampleRate)); // ~5.8 ms
    const int win = 2 * hop;
    const int frames = (n - win) / hop;
    if (frames < 4) return info;

    // Mono mix, and a treble-boosted copy so hi-hats count as much as kicks.
    std::vector<float> mono(static_cast<size_t>(n)), bright(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        float m = 0.0f;
        for (int ch = 0; ch < channels; ++ch) m += audio.getSample(ch, i);
        mono[static_cast<size_t>(i)] = m / static_cast<float>(channels);
        bright[static_cast<size_t>(i)] = mono[static_cast<size_t>(i)] - (i > 0 ? 0.97f * mono[static_cast<size_t>(i - 1)] : 0.0f);
    }

    // Loudness per frame in dB, and how much it jumped up over the last two frames.
    std::vector<float> level(static_cast<size_t>(frames)), rise(static_cast<size_t>(frames), 0.0f);
    for (int f = 0; f < frames; ++f) {
        double e = 0.0;
        for (int i = f * hop; i < f * hop + win; ++i) e += bright[static_cast<size_t>(i)] * bright[static_cast<size_t>(i)];
        level[static_cast<size_t>(f)] = 10.0f * std::log10(1e-10f + static_cast<float>(e / win));
    }
    const float floor = -100.0f; // before the sample starts counts as silence, so a hit at sample 0 is found
    auto lv = [&](int f) { return f < 0 ? floor : level[static_cast<size_t>(std::min(f, frames - 1))]; };
    for (int f = 0; f < frames; ++f) rise[static_cast<size_t>(f)] = lv(f) - lv(f - 2);
    const float loudest = *std::max_element(level.begin(), level.end());

    const int minGap = static_cast<int>(0.05 * sampleRate / hop); // 50 ms between hits
    const int ctx = static_cast<int>(0.25 * sampleRate / hop);    // +-250 ms for the local threshold
    int last = -minGap;
    for (int f = 0; f < frames; ++f) {
        const float r = rise[static_cast<size_t>(f)];
        if (r < 6.0f || level[static_cast<size_t>(f)] < loudest - 45.0f || f - last < minGap) continue;
        bool peak = true; // biggest rise within +-3 frames
        for (int k = std::max(0, f - 3); k <= std::min(frames - 1, f + 3) && peak; ++k)
            peak = rise[static_cast<size_t>(k)] <= r && !(k > f && rise[static_cast<size_t>(k)] == r);
        if (!peak) continue;
        // A real hit is louder than anything in the 50 ms before it. A swell after a dip (two tones
        // beating, a fade) jumps up in dB too, but only back to where it already was.
        const float hitLevel = std::max({lv(f), lv(f + 1), lv(f + 2)});
        float before = floor;
        for (int k = f - minGap; k <= f - 3; ++k) before = std::max(before, lv(k));
        if (hitLevel < before + 3.0f) continue;
        const int a = std::max(0, f - ctx), b = std::min(frames, f + ctx);
        const std::vector<float> around(rise.begin() + a, rise.begin() + b);
        if (r < percentile(around, 0.5f) + 4.0f) continue; // must stand out from what's around it

        // Step back to the quietest frame just before the jump, then to the first sample that is
        // clearly part of the hit, minus 0.5 ms, so playback starts right before the attack.
        int quiet = f;
        for (int k = f - 1; k >= std::max(0, f - 4); --k)
            if (level[static_cast<size_t>(k)] < level[static_cast<size_t>(quiet)]) quiet = k;
        const int from = quiet * hop, to = std::min(n, f * hop + win);
        float top = 0.0f;
        for (int i = from; i < to; ++i) top = std::max(top, std::abs(mono[static_cast<size_t>(i)]));
        float tail = 0.0f; // what's still ringing from earlier hits, just before this one
        for (int i = std::max(0, from - hop); i < from; ++i) tail = std::max(tail, std::abs(mono[static_cast<size_t>(i)]));
        const float threshold = std::max(0.2f * top, 2.0f * tail);
        int start = from;
        while (start < to && std::abs(mono[static_cast<size_t>(start)]) < threshold) ++start;
        start = std::max(0, start - static_cast<int>(0.0005 * sampleRate));
        if (!info.onsets.empty() && start - info.onsets.back() < static_cast<int>(0.05 * sampleRate)) continue;
        info.onsets.push_back(start);
        last = f;
    }

    // Drum-like: at least 2 hits, 1..25 per second, and a big gap between loud and quiet frames.
    std::vector<float> audible;
    for (float l : level)
        if (l > loudest - 60.0f) audible.push_back(l);
    const double rate = static_cast<double>(info.onsets.size()) * sampleRate / n;
    info.drumLike = info.onsets.size() >= 2 && rate >= 1.0 && rate <= 25.0
                    && percentile(audible, 0.95f) - percentile(audible, 0.2f) > 18.0f;
    return info;
}

} // namespace mensis
