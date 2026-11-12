#include <juce_audio_utils/juce_audio_utils.h>

// Thin host wrapper. All sound-making code will live in core/; this only translates host <-> core.
class DemeaProcessor final : public juce::AudioProcessor {
public:
    DemeaProcessor()
        : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    const juce::String getName() const override { return JucePlugin_Name; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();
    }

    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    bool hasEditor() const override { return true; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DemeaProcessor(); }
