#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

namespace mensis {

// Audio loaded from a file. Never changed after loading, so the audio thread can read it safely.
struct Sample {
    juce::AudioBuffer<float> audio; // 1 (mono) or 2 (stereo) channels
    double sampleRate = 44100.0;
    juce::File file;
    std::vector<int> slices; // where the hits start (sample index), found at load
    bool drumLike = false;   // guess: turn SLICE on for this sample

    double lengthSec() const { return audio.getNumSamples() / sampleRate; }
};

// Reads WAV/AIFF/FLAC/MP3/... Allocates and does file I/O: never call on the audio thread.
// Returns nullptr if the file can't be read.
std::unique_ptr<Sample> loadSample(const juce::File& file);

} // namespace mensis
