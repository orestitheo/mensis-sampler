#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace mensis {

struct SliceInfo {
    std::vector<int> onsets; // sample index where each hit starts, ascending
    bool drumLike = false;   // many sharp hits that fall back to quiet: turn SLICE on by default
};

// Finds the hits in a sample. Allocates: call off the audio thread (at load).
SliceInfo findSlices(const juce::AudioBuffer<float>& audio, double sampleRate);

} // namespace mensis
