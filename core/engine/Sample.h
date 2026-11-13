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
    juce::MemoryBlock fileData; // the original file's bytes (if small enough), so a saved Live set can carry it

    double lengthSec() const { return audio.getNumSamples() / sampleRate; }
};

// Samples up to this size are kept (as file bytes) inside the saved Live set.
inline constexpr juce::int64 kMaxEmbedBytes = 20 * 1024 * 1024;

// Reads WAV/AIFF/FLAC/MP3/... Allocates and does file I/O: never call on the audio thread.
// Returns nullptr if the file can't be read.
std::unique_ptr<Sample> loadSample(const juce::File& file);
// The same, from a copy of the file's bytes (e.g. embedded in a saved set). originalFile is just the name.
std::unique_ptr<Sample> loadSampleFromData(const juce::MemoryBlock& data, const juce::File& originalFile);

} // namespace mensis
