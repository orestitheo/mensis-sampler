#pragma once
#include <algorithm>
#include "LoopTrack.h"
#include "Space.h"

namespace mensis {

// Four fixed macros, each one big musical move. 0 = no change. Added on top of the knobs (and modulation),
// clamped to each knob's range.
struct Macros {
    float destroy = 0.0f; // drive up, bits down, squash up, character toward fuzz
    float drift = 0.0f;   // grain cloud: mix, spray, detune up
    float ring = 0.0f;    // resonator: mix up, decay longer
    float space = 0.0f;   // sends, size and feedback up
};

inline void applyMacros(const Macros& m, LoopParams& lp, float& send) {
    auto add = [](float v, float by) { return std::clamp(v + by, 0.0f, 1.0f); };
    if (m.destroy > 0.0f) {
        lp.color.drive = add(lp.color.drive, 0.8f * m.destroy);
        lp.color.character = add(lp.color.character, 0.5f * m.destroy);
        lp.color.squash = add(lp.color.squash, 0.6f * m.destroy);
        lp.color.bits = std::min(lp.color.bits, 16.0f - 12.0f * m.destroy); // down to 4 bit
    }
    if (m.drift > 0.0f) {
        lp.grains.mix = add(lp.grains.mix, 0.8f * m.drift);
        lp.grains.spray = add(lp.grains.spray, 0.6f * m.drift);
        lp.grains.detune = std::min(12.0f, lp.grains.detune + 7.0f * m.drift);
    }
    if (m.ring > 0.0f) {
        lp.res.mix = add(lp.res.mix, 0.8f * m.ring);
        lp.res.decay = std::min(10.0f, lp.res.decay * (1.0f + 4.0f * m.ring));
    }
    if (m.space > 0.0f) send = add(send, 0.6f * m.space);
}

inline void applyMacros(const Macros& m, SpaceParams& sp) {
    if (m.space <= 0.0f) return;
    sp.size = std::clamp(sp.size + 0.4f * m.space, 0.0f, 1.0f);
    sp.feedback = std::clamp(sp.feedback + 0.3f * m.space, 0.0f, 0.95f);
}

} // namespace mensis
