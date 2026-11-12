#include "Sample.h"

namespace mensis {

std::unique_ptr<Sample> loadSample(const juce::File& file) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0) return nullptr;

    // shortcut: whole file in RAM, capped at 10 min; stream from disk if longer samples are ever needed
    const auto maxSamples = static_cast<juce::int64>(600.0 * reader->sampleRate);
    const int numSamples = static_cast<int>(std::min(reader->lengthInSamples, maxSamples));
    if (numSamples <= 1) return nullptr;

    auto sample = std::make_unique<Sample>();
    const int channels = reader->numChannels > 1 ? 2 : 1;
    sample->audio.setSize(channels, numSamples);
    if (!reader->read(&sample->audio, 0, numSamples, 0, true, channels > 1)) return nullptr;
    sample->sampleRate = reader->sampleRate;
    sample->file = file;
    return sample;
}

} // namespace mensis
