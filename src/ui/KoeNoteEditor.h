#pragma once

#include "plugin/KoeNoteAudioModification.h"
#include "plugin/KoeNoteProcessor.h"
#include "ui/NoteParamPanel.h"
#include "ui/PianoRollView.h"
#include "ui/Strings.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace koenote::ui
{
// プラグインの Editor。ARA の選択に追従して、編集対象のクリップをピアノロールに表示する。
// ARA ではホストと密に統合するため、Editor はリサイズ可能にする必要がある。
class KoeNoteEditor final : public juce::AudioProcessorEditor,
                            private juce::AudioProcessorEditorARAExtension,
                            private juce::ARAEditorView::Listener,
                            private juce::Timer
{
public:
    explicit KoeNoteEditor(plugin::KoeNoteProcessor& processor);
    ~KoeNoteEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::AudioProcessorEditorARAExtension* getARAClientExtensions() override { return this; }

private:
    void onNewSelection(const juce::ARAViewSelection& selection) override;
    void timerCallback() override;

    void refreshTarget();
    void updateStatus();
    void updateToolButtons();
    void applyLanguage();
    void applySnap();
    plugin::KoeNoteAudioModification* findModification(juce::ARAPlaybackRegion** regionOut) const;

    plugin::KoeNoteProcessor& processorRef;
    Language language;

    PianoRollView roll;
    NoteParamPanel panel;

    juce::TextButton selectButton, splitButton, boundaryButton;
    juce::TextButton undoButton, redoButton, mergeButton, resetButton, fitButton, languageButton;
    juce::ComboBox snapBox, rootBox, scaleBox;
    juce::Label statusLabel;

    juce::ARAPlaybackRegion* selectedRegion = nullptr;
    juce::ARAPlaybackRegion* currentRegion = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KoeNoteEditor)
};
} // namespace koenote::ui
