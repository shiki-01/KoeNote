#include "ui/KoeNoteEditor.h"

#include <algorithm>

namespace koenote::ui
{
namespace
{
constexpr int kToolbarRowHeight = 32;
constexpr int kStatusHeight = 24;
constexpr int kPanelWidth = 300;

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
      noteParams(roll),
      batchPanel(roll, [this] { syncSnapFromModel(); }),
      midiPanel(roll),
      toolsPanel(roll)
{
    for (auto* b : { &selectButton, &splitButton, &boundaryButton, &assignButton })
    {
        styleToggle(*b);
        addAndMakeVisible(*b);
    }

    for (auto* b : { &undoButton, &redoButton, &mergeButton, &resetButton, &abButton, &fitButton, &reanalyzeButton, &languageButton })
        addAndMakeVisible(*b);

    abButton.setClickingTogglesState(true);

    selectButton.setToggleState(true, juce::dontSendNotification);
    selectButton.onClick = [this] { roll.setTool(Tool::Select); };
    splitButton.onClick = [this] { roll.setTool(Tool::Split); };
    boundaryButton.onClick = [this] { roll.setTool(Tool::Boundary); };
    assignButton.onClick = [this] { roll.setTool(Tool::Assign); };
    undoButton.onClick = [this] { roll.undo(); };
    redoButton.onClick = [this] { roll.redo(); };
    mergeButton.onClick = [this] { roll.mergeSelected(); };
    resetButton.onClick = [this] { roll.resetSelected(); };
    abButton.onClick = [this] { toolsPanel.setCompareOriginal(abButton.getToggleState()); };
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
        box->onChange = [this] { applySnapFromUi(); };
        addAndMakeVisible(*box);
    }

    const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < 12; ++i)
        rootBox.addItem(noteNames[i], i + 1);
    rootBox.setSelectedId(1, juce::dontSendNotification);

    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd5dae3));
    statusLabel.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(statusLabel);

    // サイドパネル（タブ）
    noteView.setViewedComponent(&noteParams, false);
    batchView.setViewedComponent(&batchPanel, false);
    midiView.setViewedComponent(&midiPanel, false);
    toolsView.setViewedComponent(&toolsPanel, false);

    for (auto* v : { &noteView, &batchView, &midiView, &toolsView })
    {
        v->setScrollBarsShown(true, false);
        v->setScrollBarThickness(10);
    }

    const juce::Colour tabColour(0xff20232a);
    tabs.setTabBarDepth(28);
    tabs.addTab("Note", tabColour, &noteView, false);
    tabs.addTab("Batch", tabColour, &batchView, false);
    tabs.addTab("MIDI", tabColour, &midiView, false);
    tabs.addTab("More", tabColour, &toolsView, false);
    tabs.setOutline(0);
    addAndMakeVisible(tabs);

    roll.onSelectionChanged = [this] { noteParams.refresh(); refreshPanels(); updateStatus(); };
    roll.onModelChanged = [this] { noteParams.refresh(); refreshPanels(); updateStatus(); };

    addAndMakeVisible(roll);

    applyLanguage();

    if (auto* view = getARAEditorView())
        view->addListener(this);

    setResizable(true, false);
    setResizeLimits(760, 420, 4000, 2000);
    setSize(1240, 600);

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
    assignButton.setButtonText(s.toolAssign);
    undoButton.setButtonText(s.undo);
    redoButton.setButtonText(s.redo);
    mergeButton.setButtonText(s.merge);
    resetButton.setButtonText(s.reset);
    abButton.setButtonText(s.abButton);
    fitButton.setButtonText(s.zoomFit);
    reanalyzeButton.setButtonText(s.reanalyze);
    languageButton.setButtonText(s.language);

    selectButton.setTooltip(s.tipSelect);
    splitButton.setTooltip(s.tipSplit);
    boundaryButton.setTooltip(s.tipBoundary);
    assignButton.setTooltip(s.tipAssign);
    mergeButton.setTooltip(s.tipMerge);
    resetButton.setTooltip(s.tipReset);
    reanalyzeButton.setTooltip(s.tipReanalyze);
    abButton.setTooltip(s.tipAB);

    const juce::ScopedValueSetter<bool> guard(syncingSnap, true);

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

    tabs.setTabName(0, s.tabNote);
    tabs.setTabName(1, s.tabBatch);
    tabs.setTabName(2, s.tabMidi);
    tabs.setTabName(3, s.tabMore);

    roll.setLanguage(language);
    noteParams.setLanguage(language);
    batchPanel.setLanguage(language);
    midiPanel.setLanguage(language);
    toolsPanel.setLanguage(language);
    updateStatus();
    repaint();
}

void KoeNoteEditor::applySnapFromUi()
{
    if (syncingSnap)
        return;

    const auto mode = static_cast<SnapMode>(std::clamp(snapBox.getSelectedId() - 1, 0, 2));
    const int root = std::max(0, rootBox.getSelectedId() - 1);
    const auto scale = scaleBox.getSelectedId() == 2 ? ScaleType::Minor : ScaleType::Major;

    rootBox.setEnabled(mode == SnapMode::Scale);
    scaleBox.setEnabled(mode == SnapMode::Scale);
    roll.setSnap(mode, root, scale);

    // スナップ設定はプロジェクトに保存する
    if (auto* target = roll.getModification())
    {
        auto settings = target->getModel().settings();
        settings.snapMode = static_cast<std::uint8_t>(mode);
        settings.keyRoot = static_cast<std::uint8_t>(root);
        settings.keyMinor = scale == ScaleType::Minor ? 1 : 0;
        target->getModel().setSettings(settings);
    }

    roll.repaint();
}

void KoeNoteEditor::syncSnapFromModel()
{
    auto* target = roll.getModification();
    if (target == nullptr)
        return;

    const auto& settings = target->getModel().settings();

    {
        const juce::ScopedValueSetter<bool> guard(syncingSnap, true);
        snapBox.setSelectedId(std::clamp<int>(settings.snapMode, 0, 2) + 1, juce::dontSendNotification);
        rootBox.setSelectedId(std::clamp<int>(settings.keyRoot, 0, 11) + 1, juce::dontSendNotification);
        scaleBox.setSelectedId(settings.keyMinor != 0 ? 2 : 1, juce::dontSendNotification);
    }

    const auto mode = static_cast<SnapMode>(std::clamp<int>(settings.snapMode, 0, 2));
    rootBox.setEnabled(mode == SnapMode::Scale);
    scaleBox.setEnabled(mode == SnapMode::Scale);
    roll.setSnap(mode, settings.keyRoot, settings.keyMinor != 0 ? ScaleType::Minor : ScaleType::Major);
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

void KoeNoteEditor::layoutPanels()
{
    // 各タブのパネルは、ビューポートの幅に合わせ、内容の高さ（足りなければビューポートの高さ）にする
    const struct { juce::Viewport* view; SidePanel* panel; juce::Component* plain; } items[] = {
        { &noteView, nullptr, &noteParams }, { &batchView, &batchPanel, nullptr }, { &midiView, &midiPanel, nullptr }, { &toolsView, &toolsPanel, nullptr }
    };

    for (const auto& item : items)
    {
        const int width = std::max(100, item.view->getMaximumVisibleWidth());
        const int preferred = item.panel != nullptr ? item.panel->getPreferredHeight() : noteParams.getPreferredHeight();
        const int height = std::max(preferred, item.view->getHeight());
        (item.panel != nullptr ? static_cast<juce::Component*>(item.panel) : item.plain)->setSize(width, height);
    }
}

void KoeNoteEditor::resized()
{
    auto area = getLocalBounds();

    if (! isARAEditorView())
    {
        for (auto* c : std::initializer_list<juce::Component*> { &selectButton, &splitButton, &boundaryButton, &assignButton, &undoButton, &redoButton,
                                                                 &mergeButton, &resetButton, &abButton, &fitButton, &reanalyzeButton, &languageButton,
                                                                 &snapBox, &rootBox, &scaleBox, &statusLabel, &roll, &tabs })
            c->setVisible(false);
        return;
    }

    auto row1 = area.removeFromTop(kToolbarRowHeight).reduced(6, 3);
    auto row2 = area.removeFromTop(kToolbarRowHeight).reduced(6, 3);
    statusLabel.setBounds(area.removeFromBottom(kStatusHeight).reduced(8, 2));

    // 1 段目: ツール、スナップ、言語
    for (auto* b : { &selectButton, &splitButton, &boundaryButton, &assignButton })
        b->setBounds(row1.removeFromLeft(72).reduced(1, 0));

    row1.removeFromLeft(12);
    snapBox.setBounds(row1.removeFromLeft(112));
    row1.removeFromLeft(4);
    rootBox.setBounds(row1.removeFromLeft(58));
    row1.removeFromLeft(4);
    scaleBox.setBounds(row1.removeFromLeft(96));
    languageButton.setBounds(row1.removeFromRight(76));

    // 2 段目: 編集操作、A/B、表示、再解析
    for (auto* b : { &undoButton, &redoButton, &mergeButton, &resetButton })
        b->setBounds(row2.removeFromLeft(84).reduced(1, 0));

    row2.removeFromLeft(12);
    abButton.setBounds(row2.removeFromLeft(56));
    row2.removeFromLeft(12);
    fitButton.setBounds(row2.removeFromLeft(80));
    reanalyzeButton.setBounds(row2.removeFromRight(88));

    const int panelWidth = std::min(kPanelWidth, getWidth() / 2);
    tabs.setBounds(area.removeFromRight(panelWidth));
    roll.setBounds(area);

    layoutPanels();
}

//==============================================================================
bool KoeNoteEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return midiPanel.isInterestedInFileDrag(files);
}

void KoeNoteEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    midiPanel.filesDropped(files, x, y);
    tabs.setCurrentTabIndex(2);
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
    {
        roll.setModification(modification);
        syncSnapFromModel();
    }

    noteParams.refresh();
    refreshPanels();
    updateStatus();
}

void KoeNoteEditor::refreshPanels()
{
    batchPanel.refresh();
    midiPanel.refresh();
    toolsPanel.refresh();
}

void KoeNoteEditor::timerCallback()
{
    // 選択やリージョンの増減に追従する（ホストがリージョンを破棄しても参照が残らないよう毎回解決する）
    juce::ARAPlaybackRegion* region = nullptr;
    auto* modification = isARAEditorView() ? findModification(&region) : nullptr;
    currentRegion = region;

    if (roll.getModification() != modification)
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

    const auto* target = roll.getModification();
    undoButton.setEnabled(target != nullptr && target->getModel().canUndo());
    redoButton.setEnabled(target != nullptr && target->getModel().canRedo());
    abButton.setToggleState(toolsPanel.isCompareOriginal(), juce::dontSendNotification);

    // ステータスとパネルの表示は、約 3 回/秒で更新する（ニューラルや整列の進捗など）
    if (++tickCounter % 10 == 0)
    {
        refreshPanels();
        updateStatus();
    }
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

            if (const auto neural = modification->getNeuralInfo(); neural.state == plugin::NeuralInfo::State::Rendering)
                text += " - " + s.neuralRendering + " " + juce::String(juce::roundToInt(neural.progress * 100.0f)) + "%";
            break;
    }

    statusLabel.setText(text, juce::dontSendNotification);
}
} // namespace koenote::ui
