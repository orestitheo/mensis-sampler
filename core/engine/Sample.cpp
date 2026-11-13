#include "Sample.h"
#include "Slices.h"

namespace mensis {

namespace {

std::unique_ptr<Sample> fromReader(juce::AudioFormatReader* reader, std::unique_ptr<Sample> sample) {
    if (reader == nullptr || reader->sampleRate <= 0.0) return nullptr;

    // shortcut: whole file in RAM, capped at 10 min; stream from disk if longer samples are ever needed
    const auto maxSamples = static_cast<juce::int64>(600.0 * reader->sampleRate);
    const int numSamples = static_cast<int>(std::min(reader->lengthInSamples, maxSamples));
    if (numSamples <= 1) return nullptr;

    const int channels = reader->numChannels > 1 ? 2 : 1;
    sample->audio.setSize(channels, numSamples);
    if (!reader->read(&sample->audio, 0, numSamples, 0, true, channels > 1)) return nullptr;
    sample->sampleRate = reader->sampleRate;
    auto slices = findSlices(sample->audio, sample->sampleRate);
    sample->slices = std::move(slices.onsets);
    sample->drumLike = slices.drumLike;
    return sample;
}

} // namespace

std::unique_ptr<Sample> loadSample(const juce::File& file) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    auto sample = std::make_unique<Sample>();
    sample->file = file;
    if (file.getSize() <= kMaxEmbedBytes) file.loadFileAsData(sample->fileData); // carried in the saved set
    return fromReader(reader.get(), std::move(sample));
}

std::unique_ptr<Sample> loadSampleFromData(const juce::MemoryBlock& data, const juce::File& originalFile) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto sample = std::make_unique<Sample>();
    sample->file = originalFile;
    sample->fileData = data;
    std::unique_ptr<juce::AudioFormatReader> reader(
        formats.createReaderFor(std::make_unique<juce::MemoryInputStream>(sample->fileData, false)));
    return fromReader(reader.get(), std::move(sample));
}

} // namespace mensis
