#include "ui/KoeNoteEditor.h"

#include <algorithm>

namespace koenote::ui
{
namespace
{
constexpr int kToolbarHeight = 36;
constexpr int kStatusHeight = 24;
constexpr int kPanelWidth = 220;

void styleToggle(juce::TextButton& b)
{
    b.setClickingTogglesState(true);
    b.setRadioGroupId(1001);
}
} // namespace

KoeNoteEditor::KoeNoteEditor(plugin::KoeNoteProcessor& processor)
    : juce::AudioProcessorEditor(&processor),
      juce::AudioProcessorEditorARAExtension(&processor),
      processorRef(processor),
      language(detectDefaultLanguage()),
      panel(roll)
{
    for (auto* b : { &selectButton, &splitButton, &boundaryButton })
    {
        styleToggle(*b);
        addAndMakeVisible(*b);
    }

    for (auto* b : { &undoButton, &redoButton, &mergeButton, &resetButton, &fitButton, &reanalyzeButton, &languageButton })
        addAndMakeVisible(*b);

    selectButton.setToggleState(true, juce::dontSendNotification);
    selectButton.onClick = [this] { roll.setTool(Tool::Select); };
    splitButton.onClick = [this] { roll.setTool(Tool::Split); };
    boundaryButton.onClick = [this] { roll.setTool(Tool::Boundary); };
    undoButton.onClick = [this] { roll.undo(); };
    redoButton.onClick = [this] { roll.redo(); };
    mergeButton.onClick = [this] { roll.mergeSelected(); };
    resetButton.onClick = [this] { roll.resetSelected(); };
    fitButton.onClick = [this] { roll.zoomToFit(); };
    reanalyzeButton.onClick = [this]
    {
        if (roll.getModification() == nullptr)
            return;

        const auto& s = getStrings(language);
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel(juce::MessageBoxIconType::WarningIcon, s.reanalyzeTitle, s.reanalyzeMessage, s.ok, s.cancel, this);

        // 非同期のダイアログ。確認後にモデルが差し替わっている場合に備え、実行時に対象を取り直す。
        juce::Component::SafePointer<KoeNoteEditor> safe(this);

        juce::AlertWindow::showAsync(options, [safe] (int result)
        {
            if (safe != nullptr && result == 1 && safe->roll.getModification() != nullptr)
                safe->roll.getModification()->requestReanalysis();
        });
    };
    languageButton.onClick = [this]
    {
        language = language == Language::Japanese ? Language::English : Language::Japanese;
        applyLanguage();
    };

    for (auto* box : { &snapBox, &rootBox, &scaleBox })
    {
        box->onChange = [this] { applySnap(); };
        addAndMakeVisible(*box);
    }

    const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < 12; ++i)
        rootBox.addItem(noteNames[i], i + 1);
    rootBox.setSelectedId(1, juce::dontSendNotification);

    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd5dae3));
    statusLabel.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(statusLabel);

    roll.onSelectionChanged = [this] { panel.refresh(); updateStatus(); };
    roll.onModelChanged = [this] { panel.refresh(); updateStatus(); };

    addAndMakeVisible(roll);
    addAndMakeVisible(panel);

    applyLanguage();
    applySnap();

    if (auto* view = getARAEditorView())
        view->addListener(this);

    setResizable(true, false);
    setResizeLimits(640, 360, 4000, 2000);
    setSize(1000, 540);

    refreshTarget();
    startTimerHz(30);
}

KoeNoteEditor::~KoeNoteEditor()
{
    stopTimer();

    if (auto* view = getARAEditorView())
        view->removeListener(this);

    roll.setModification(nullptr);
}

void KoeNoteEditor::applyLanguage()
{
    const auto& s = getStrings(language);

    selectButton.setButtonText(s.toolSelect);
    splitButton.setButtonText(s.toolSplit);
    boundaryButton.setButtonText(s.toolBoundary);
    undoButton.setButtonText(s.undo);
    redoButton.setButtonText(s.redo);
    mergeButton.setButtonText(s.merge);
    resetButton.setButtonText(s.reset);
    fitButton.setButtonText(s.zoomFit);
    reanalyzeButton.setButtonText(s.reanalyze);
    reanalyzeButton.setTooltip(s.tipReanalyze);
    languageButton.setButtonText(s.language);

    selectButton.setTooltip(s.tipSelect);
    splitButton.setTooltip(s.tipSplit);
    boundaryButton.setTooltip(s.tipBoundary);
    mergeButton.setTooltip(s.tipMerge);
    resetButton.setTooltip(s.tipReset);

    const int snapId = snapBox.getSelectedId();
    snapBox.clear(juce::dontSendNotification);
    snapBox.addItem(s.snapOff, 1);
    snapBox.addItem(s.snapSemitone, 2);
    snapBox.addItem(s.snapScale, 3);
    snapBox.setSelectedId(snapId > 0 ? snapId : 2, juce::dontSendNotification);

    const int scaleId = scaleBox.getSelectedId();
    scaleBox.clear(juce::dontSendNotification);
    scaleBox.addItem(s.scaleMajor, 1);
    scaleBox.addItem(s.scaleMinor, 2);
    scaleBox.setSelectedId(scaleId > 0 ? scaleId : 1, juce::dontSendNotification);

    roll.setLanguage(language);
    panel.setLanguage(language);
    updateStatus();
    repaint();
}

void KoeNoteEditor::applySnap()
{
    const auto mode = static_cast<SnapMode>(std::clamp(snapBox.getSelectedId() - 1, 0, 2));
    const int root = std::max(0, rootBox.getSelectedId() - 1);
    const auto scale = scaleBox.getSelectedId() == 2 ? ScaleType::Minor : ScaleType::Major;

    rootBox.setEnabled(mode == SnapMode::Scale);
    scaleBox.setEnabled(mode == SnapMode::Scale);
    roll.setSnap(mode, root, scale);
    roll.repaint();
}

void KoeNoteEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff15171b));

    if (! isARAEditorView())
    {
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(15.0f));
        g.drawFittedText(getStrings(language).notARA, getLocalBounds().reduced(40), juce::Justification::centred, 3);
    }
}

void KoeNoteEditor::resized()
{
    auto area = getLocalBounds();

    if (! isARAEditorView())
    {
        for (auto* c : std::initializer_list<juce::Component*> { &selectButton, &splitButton, &boundaryButton, &undoButton, &redoButton, &mergeButton,
                                                                 &resetButton, &fitButton, &reanalyzeButton, &languageButton, &snapBox, &rootBox, &scaleBox, &statusLabel, &roll, &panel })
            c->setVisible(false);
        return;
    }

    auto toolbar = area.removeFromTop(kToolbarHeight).reduced(6, 4);
    auto status = area.removeFromBottom(kStatusHeight).reduced(8, 2);
    statusLabel.setBounds(status);

    for (auto* b : { &selectButton, &splitButton, &boundaryButton })
        b->setBounds(toolbar.removeFromLeft(68).reduced(1, 0));

    toolbar.removeFromLeft(10);
    snapBox.setBounds(toolbar.removeFromLeft(110));
    toolbar.removeFromLeft(4);
    rootBox.setBounds(toolbar.removeFromLeft(58));
    toolbar.removeFromLeft(4);
    scaleBox.setBounds(toolbar.removeFromLeft(96));
    toolbar.removeFromLeft(12);

    languageButton.setBounds(toolbar.removeFromRight(72));
    toolbar.removeFromRight(6);
    fitButton.setBounds(toolbar.removeFromRight(72));
    toolbar.removeFromRight(6);
    reanalyzeButton.setBounds(toolbar.removeFromRight(78));
    toolbar.removeFromRight(6);

    for (auto* b : { &undoButton, &redoButton, &mergeButton, &resetButton })
        b->setBounds(toolbar.removeFromLeft(78).reduced(1, 0));

    panel.setBounds(area.removeFromRight(kPanelWidth));
    roll.setBounds(area);
}

//==============================================================================
void KoeNoteEditor::onNewSelection(const juce::ARAViewSelection& selection)
{
    const auto regions = selection.getEffectivePlaybackRegions<juce::ARAPlaybackRegion>();
    selectedRegion = regions.empty() ? nullptr : regions.front();
    refreshTarget();
}

plugin::KoeNoteAudioModification* KoeNoteEditor::findModification(juce::ARAPlaybackRegion** regionOut) const
{
    // この Editor が属するインスタンスのリージョンから選ぶ。選択中のリージョンがあればそれを優先する。
    std::vector<juce::ARAPlaybackRegion*> candidates;

    if (auto* playback = processorRef.getARAPlaybackRendererForUI())
        for (auto* r : playback->getPlaybackRegions())
            candidates.push_back(r);

    if (auto* editorRenderer = processorRef.getARAEditorRendererForUI())
        for (auto* r : editorRenderer->getPlaybackRegions())
            if (std::find(candidates.begin(), candidates.end(), r) == candidates.end())
                candidates.push_back(r);

    if (candidates.empty())
        return nullptr;

    juce::ARAPlaybackRegion* chosen = candidates.front();

    if (selectedRegion != nullptr && std::find(candidates.begin(), candidates.end(), selectedRegion) != candidates.end())
        chosen = selectedRegion;

    if (regionOut != nullptr)
        *regionOut = chosen;

    return static_cast<plugin::KoeNoteAudioModification*>(chosen->getAudioModification());
}

void KoeNoteEditor::refreshTarget()
{
    juce::ARAPlaybackRegion* region = nullptr;
    auto* modification = isARAEditorView() ? findModification(&region) : nullptr;

    currentRegion = region;

    if (roll.getModification() != modification)
        roll.setModification(modification);

    panel.refresh();
    updateStatus();
}

void KoeNoteEditor::timerCallback()
{
    // 選択やリージョンの増減に追従する（ホストがリージョンを破棄しても参照が残らないよう毎回解決する）
    refreshTarget();

    double playhead = -1.0;

    if (currentRegion != nullptr && processorRef.isPlayheadPlaying())
    {
        const double project = processorRef.getPlayheadSeconds();
        const double start = currentRegion->getStartInPlaybackTime();
        const double duration = currentRegion->getDurationInPlaybackTime();

        if (project >= start && project <= start + duration)
            playhead = project - start + currentRegion->getStartInAudioModificationTime();
    }

    roll.setPlayhead(playhead);

    undoButton.setEnabled(roll.getModification() != nullptr && roll.getModification()->getModel().canUndo());
    redoButton.setEnabled(roll.getModification() != nullptr && roll.getModification()->getModel().canRedo());
}

void KoeNoteEditor::updateStatus()
{
    const auto& s = getStrings(language);
    auto* modification = roll.getModification();

    if (modification == nullptr)
    {
        statusLabel.setText(s.noClip, juce::dontSendNotification);
        return;
    }

    juce::String text;

    switch (modification->getAnalysisState())
    {
        case plugin::AnalysisState::WaitingForAudio: text = s.statusWaiting; break;
        case plugin::AnalysisState::Analyzing:
            text = s.statusAnalyzing + " " + juce::String(juce::roundToInt(modification->getAnalysisProgress() * 100.0f)) + "%";
            break;
        case plugin::AnalysisState::Failed: text = s.statusFailed; break;
        case plugin::AnalysisState::Done:
            text = s.statusDone + " - " + juce::String(static_cast<int>(modification->getModel().notes().size())) + " " + s.notes;
            if (modification->isRenderPending())
                text += " - " + s.statusRendering;
            break;
    }

    statusLabel.setText(text, juce::dontSendNotification);
}
} // namespace koenote::ui
