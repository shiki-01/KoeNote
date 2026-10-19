#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace koenote::plugin
{
// ARA2 対応の AudioProcessor。ARA 非対応のホストでは何もしない（キャプチャモードは F-19 で対応予定）。
class KoeNoteProcessor final : public juce::AudioProcessor,
                               private juce::AudioProcessorARAExtension
{
public:
    KoeNoteProcessor();
    ~KoeNoteProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using juce::AudioProcessor::processBlock;

    const juce::String getName() const override { return "KoeNote"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // 編集内容は ARA のアーカイブに保存されるため、プロセッサ自身の状態はない
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    juce::AudioProcessorARAExtension* getARAClientExtensions() override { return this; }

private:
    static BusesProperties makeBusesProperties();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KoeNoteProcessor)
};
} // namespace koenote::plugin
