#pragma once

#include "plugin/KoeNoteAudioModification.h"
#include "plugin/KoeNoteProcessor.h"
#include "ui/NoteParamPanel.h"
#include "ui/PianoRollView.h"
#include "ui/SidePanels.h"
#include "ui/Strings.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace koenote::ui
{
// プラグインの Editor。ARA の選択に追従して、編集対象のクリップをピアノロールに表示する。
// ARA ではホストと密に統合するため、Editor はリサイズ可能にする必要がある。
class KoeNoteEditor final : public juce::AudioProcessorEditor,
                            private juce::AudioProcessorEditorARAExtension,
                            private juce::ARAEditorView::Listener,
                            public juce::DragAndDropContainer,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer
{
public:
    explicit KoeNoteEditor(plugin::KoeNoteProcessor& processor);
    ~KoeNoteEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // MIDI ファイルのドロップ（F-12）
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    juce::AudioProcessorEditorARAExtension* getARAClientExtensions() override { return this; }

private:
    void onNewSelection(const juce::ARAViewSelection& selection) override;
    void timerCallback() override;

    void refreshTarget();
    void refreshPanels();
    void updateStatus();
    void applyLanguage();
    void applySnapFromUi();
    void syncSnapFromModel();
    void layoutPanels();
    plugin::KoeNoteAudioModification* findModification(juce::ARAPlaybackRegion** regionOut) const;

    plugin::KoeNoteProcessor& processorRef;
    Language language;

    PianoRollView roll;
    NoteParamPanel noteParams;
    BatchPanel batchPanel;
    MidiPanel midiPanel;
    ToolsPanel toolsPanel;

    juce::Viewport noteView, batchView, midiView, toolsView;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    juce::TextButton selectButton, splitButton, boundaryButton, assignButton, guideButton;
    juce::TextButton undoButton, redoButton, mergeButton, resetButton, abButton, fitButton, reanalyzeButton, languageButton;
    juce::ComboBox snapBox, rootBox, scaleBox;
    juce::Label statusLabel;
    juce::TooltipWindow tooltipWindow { this, 600 };

    juce::ARAPlaybackRegion* selectedRegion = nullptr;
    juce::ARAPlaybackRegion* currentRegion = nullptr;
    bool syncingSnap = false;
    int tickCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KoeNoteEditor)
};
} // namespace koenote::ui
