#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace koenote::ui
{
// プラグインの Editor。フェーズ0 では ARA の接続状態だけを表示する。
// ARA ではホストと密に統合するため、Editor はリサイズ可能にする必要がある。
class KoeNoteEditor final : public juce::AudioProcessorEditor,
                            private juce::AudioProcessorEditorARAExtension
{
public:
    explicit KoeNoteEditor(juce::AudioProcessor& processor);

    void paint(juce::Graphics& g) override;
    void resized() override {}

    juce::AudioProcessorEditorARAExtension* getARAClientExtensions() override { return this; }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KoeNoteEditor)
};
} // namespace koenote::ui
