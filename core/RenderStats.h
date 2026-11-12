#pragma once
#include <algorithm>
#include <cmath>
#include <span>

namespace mensis {

// Sanity numbers for any rendered audio. Claude can't listen, so every render gets checked with these.
struct RenderStats {
    bool finite = true;    // false if any NaN/Inf
    float peak = 0.0f;     // linear, 1.0 == 0 dBFS
    double dcOffset = 0.0; // mean sample value
    float maxStep = 0.0f;  // biggest jump between neighbouring samples; a spike here usually means a click
};

inline RenderStats measure(std::span<const float> samples) {
    RenderStats s;
    double sum = 0.0;
    float prev = 0.0f;
    bool havePrev = false;
    for (float x : samples) {
        if (!std::isfinite(x)) { s.finite = false; continue; }
        s.peak = std::max(s.peak, std::abs(x));
        if (havePrev) s.maxStep = std::max(s.maxStep, std::abs(x - prev));
        prev = x;
        havePrev = true;
        sum += x;
    }
    if (!samples.empty()) s.dcOffset = sum / static_cast<double>(samples.size());
    return s;
}

} // namespace mensis
