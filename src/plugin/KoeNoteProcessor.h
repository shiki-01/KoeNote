#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

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

    // Editor（message thread）から参照する
    juce::ARAPlaybackRenderer* getARAPlaybackRendererForUI() const noexcept { return getPlaybackRenderer(); }
    juce::ARAEditorRenderer* getARAEditorRendererForUI() const noexcept { return getEditorRenderer(); }

    // オーディオスレッドが更新する再生位置（プロジェクト時間の秒）
    double getPlayheadSeconds() const noexcept { return playheadSeconds.load(std::memory_order_relaxed); }
    bool isPlayheadPlaying() const noexcept { return playheadPlaying.load(std::memory_order_relaxed); }

private:
    static BusesProperties makeBusesProperties();

    std::atomic<double> playheadSeconds { 0.0 };
    std::atomic<bool> playheadPlaying { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KoeNoteProcessor)
};
} // namespace koenote::plugin
