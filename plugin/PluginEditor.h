#pragma once
#include "PluginProcessor.h"

// Temporary UI: a Load button + file name per track (drop files onto a row), then JUCE's generic knobs.
class MensisEditor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit MensisEditor(MensisProcessor& p) : AudioProcessorEditor(p), proc(p), knobs(p) {
        for (size_t t = 0; t < MensisProcessor::kNumTracks; ++t) {
            rows[t].load.setButtonText("Load T" + juce::String(t + 1));
            rows[t].load.onClick = [this, t] { chooseFile(t); };
            addAndMakeVisible(rows[t].load);
            addAndMakeVisible(rows[t].name);
        }
        addAndMakeVisible(knobs);
        setSize(560, 720);
        timerCallback();
        startTimerHz(4); // picks up samples restored from a saved Live set
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    }

    void resized() override {
        auto area = getLocalBounds().reduced(8);
        for (auto& row : rows) {
            auto line = area.removeFromTop(28);
            row.load.setBounds(line.removeFromLeft(90));
            row.name.setBounds(line.withTrimmedLeft(8));
            area.removeFromTop(4);
        }
        knobs.setBounds(area.withTrimmedTop(8));
    }

    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }

    void filesDropped(const juce::StringArray& files, int, int y) override {
        size_t track = 0; // dropped below the rows: first empty track, else track 1
        for (size_t t = MensisProcessor::kNumTracks; t-- > 0;)
            if (proc.getSampleFile(t) == juce::File{}) track = t;
        for (size_t t = 0; t < MensisProcessor::kNumTracks; ++t)
            if (y >= rows[t].load.getY() && y < rows[t].load.getBottom()) track = t;
        load(track, juce::File(files[0]));
    }

private:
    void chooseFile(size_t track) {
        chooser = std::make_unique<juce::FileChooser>("Load sample for T" + juce::String(track + 1), juce::File{},
                                                      "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this, track](const juce::FileChooser& fc) {
                                 if (fc.getResult().existsAsFile()) load(track, fc.getResult());
                             });
    }

    void load(size_t track, const juce::File& file) {
        if (!proc.loadSample(track, file, true))
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Mensis Sampler",
                                                   "Couldn't load " + file.getFileName());
        timerCallback();
    }

    void timerCallback() override {
        for (size_t t = 0; t < MensisProcessor::kNumTracks; ++t) {
            const auto file = proc.getSampleFile(t);
            rows[t].name.setText(file == juce::File{} ? "(empty: drop a file here)" : file.getFileName(),
                                 juce::dontSendNotification);
        }
    }

    struct Row { juce::TextButton load; juce::Label name; };
    MensisProcessor& proc;
    std::array<Row, MensisProcessor::kNumTracks> rows;
    juce::GenericAudioProcessorEditor knobs;
    std::unique_ptr<juce::FileChooser> chooser;
};
