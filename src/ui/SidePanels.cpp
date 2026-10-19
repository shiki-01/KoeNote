#include "ui/SidePanels.h"

#include "analysis/KeyDetector.h"
#include "model/BatchCorrection.h"
#include "model/MidiWorkflow.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>

namespace koenote::ui
{
namespace
{
const char* const kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

void styleToggle(juce::ToggleButton& b)
{
    b.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffd5dae3));
    b.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xfff2b441));
}

void styleEditor(juce::TextEditor& e)
{
    e.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff15171b));
    e.setColour(juce::TextEditor::textColourId, juce::Colour(0xffd5dae3));
    e.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff3c4452));
    e.setSelectAllWhenFocused(true);
}

double parseNumber(const juce::String& text, double fallback)
{
    const auto trimmed = text.trim();
    return trimmed.containsOnly("0123456789.-+") && trimmed.isNotEmpty() ? trimmed.getDoubleValue() : fallback;
}
} // namespace

//==============================================================================
// 一括補正
BatchPanel::BatchPanel(PianoRollView& r, std::function<void()> onSnapSettingsChanged)
    : SidePanel(r), snapSettingsChanged(std::move(onSnapSettingsChanged))
{
    language = detectDefaultLanguage();

    styleSectionLabel(title);
    addAndMakeVisible(title);

    using F = SliderRow::Format;
    pitchStrength.init(*this, 0.0, 1.0, 0.01, 1.0, F::Percent);
    vibratoKeep.init(*this, 0.0, 2.0, 0.01, 1.0, F::Percent);
    timingStrength.init(*this, 0.0, 1.0, 0.01, 0.0, F::Percent);
    pitchStrength.slider.setValue(1.0, juce::dontSendNotification);
    vibratoKeep.slider.setValue(1.0, juce::dontSendNotification);
    timingStrength.slider.setValue(0.0, juce::dontSendNotification);

    for (auto* l : { &gridLabel, &bpmLabel, &keyResult })
    {
        l->setColour(juce::Label::textColourId, juce::Colour(0xffd5dae3));
        l->setFont(juce::FontOptions(12.0f));
        addAndMakeVisible(*l);
    }

    styleHint(tempoHint);
    addAndMakeVisible(tempoHint);

    gridBox.onChange = [this]
    {
        if (updating || target() == nullptr)
            return;

        auto s = target()->getModel().settings();
        s.gridDivision = static_cast<std::uint8_t>(std::max(0, gridBox.getSelectedId() - 1));
        target()->getModel().setSettings(s);
        roll.refreshView();
    };
    addAndMakeVisible(gridBox);

    keepDuration.setToggleState(true, juce::dontSendNotification);
    styleToggle(keepDuration);
    addAndMakeVisible(keepDuration);

    styleEditor(bpmEditor);
    bpmEditor.setInputRestrictions(6, "0123456789.");
    bpmEditor.onReturnKey = [this] { commitBpm(); };
    bpmEditor.onFocusLost = [this] { commitBpm(); };
    addAndMakeVisible(bpmEditor);

    applySelected.onClick = [this] { apply(true); };
    applyAll.onClick = [this] { apply(false); };
    detect.onClick = [this] { detectKey(); };
    for (auto* b : { &applySelected, &applyAll, &detect })
        addAndMakeVisible(*b);

    updateTexts();
    refresh();
}

void BatchPanel::setLanguage(Language l)
{
    language = l;
    updateTexts();
    refresh();
}

void BatchPanel::updateTexts()
{
    const auto& s = getStrings(language);
    title.setText(s.batchTitle, juce::dontSendNotification);
    pitchStrength.label.setText(s.batchPitchStrength, juce::dontSendNotification);
    vibratoKeep.label.setText(s.batchVibratoKeep, juce::dontSendNotification);
    timingStrength.label.setText(s.batchTimingStrength, juce::dontSendNotification);
    gridLabel.setText(s.batchGrid, juce::dontSendNotification);
    bpmLabel.setText(s.batchBpm, juce::dontSendNotification);
    keepDuration.setButtonText(s.batchKeepDuration);
    applySelected.setButtonText(s.batchApplySelected);
    applyAll.setButtonText(s.batchApplyAll);
    detect.setButtonText(s.batchDetectKey);

    const juce::ScopedValueSetter<bool> guard(updating, true);
    const int selected = gridBox.getSelectedId();
    gridBox.clear(juce::dontSendNotification);
    gridBox.addItem(s.grid4, 1);
    gridBox.addItem(s.grid8, 2);
    gridBox.addItem(s.grid16, 3);
    gridBox.addItem(s.grid32, 4);
    gridBox.setSelectedId(selected > 0 ? selected : 3, juce::dontSendNotification);
}

void BatchPanel::refresh()
{
    auto* t = target();
    const juce::ScopedValueSetter<bool> guard(updating, true);

    const bool hasModel = t != nullptr && t->getModel().hasAnalysis();
    for (auto* c : std::initializer_list<juce::Component*> { &gridBox, &keepDuration, &bpmEditor, &applySelected, &applyAll, &detect })
        c->setEnabled(hasModel);

    if (! hasModel)
        return;

    const auto& settings = t->getModel().settings();
    gridBox.setSelectedId(std::clamp<int>(settings.gridDivision, 0, 3) + 1, juce::dontSendNotification);

    if (! bpmEditor.hasKeyboardFocus(true))
        bpmEditor.setText(settings.manualBpm > 0.0f ? juce::String(settings.manualBpm, 1) : juce::String(), false);

    const bool hostTempo = t->getTempoMap().valid() && settings.manualBpm <= 0.0f;
    tempoHint.setText(t->getTempoMap().valid() ? juce::String() : getStrings(language).batchNoTempo, juce::dontSendNotification);
    (void) hostTempo;
}

void BatchPanel::commitBpm()
{
    auto* t = target();
    if (t == nullptr || updating)
        return;

    const double bpm = parseNumber(bpmEditor.getText(), 0.0);
    auto s = t->getModel().settings();
    s.manualBpm = (bpm >= 20.0 && bpm <= 400.0) ? static_cast<float>(bpm) : 0.0f;
    t->getModel().setSettings(s);
    roll.refreshView();
    refresh();
}

void BatchPanel::apply(bool selectedOnly)
{
    auto* t = target();
    if (t == nullptr || ! t->getModel().hasAnalysis())
        return;

    auto& m = t->getModel();
    std::vector<model::NoteId> ids;

    if (selectedOnly)
    {
        ids.assign(roll.getSelection().begin(), roll.getSelection().end());
        if (ids.empty())
            return;
    }

    static constexpr double kGrids[] = { 1.0, 0.5, 0.25, 0.125 };
    const auto& settings = m.settings();

    model::BatchParams p;
    p.pitchStrength = static_cast<float>(pitchStrength.slider.getValue());
    p.vibratoKeep = static_cast<float>(vibratoKeep.slider.getValue());
    p.timingStrength = static_cast<float>(timingStrength.slider.getValue());
    p.snapMode = static_cast<model::SnapMode>(std::clamp<int>(settings.snapMode, 0, 2));
    p.snapRoot = settings.keyRoot;
    p.snapScale = settings.keyMinor != 0 ? model::ScaleType::Minor : model::ScaleType::Major;
    p.gridQuarters = kGrids[std::clamp<int>(settings.gridDivision, 0, 3)];
    p.keepDuration = keepDuration.getToggleState();
    p.tempo = t->getTempoMap();
    p.offsetSec = t->getTimeOffsetSec();

    const auto edits = model::computeBatchEdits(m.notes(), ids, p);

    if (m.applyEdits(edits))
        t->notifyEdited();
}

void BatchPanel::detectKey()
{
    auto* t = target();
    if (t == nullptr || ! t->getModel().hasAnalysis())
        return;

    std::vector<analysis::NoteForKey> notes;
    for (const auto& n : t->getModel().notes())
        notes.push_back({ n.detectedMidi, n.endSec - n.startSec });

    const auto result = analysis::detectKey(notes);
    const auto& s = getStrings(language);

    if (! result.valid)
    {
        keyResult.setText(s.batchKeyNone, juce::dontSendNotification);
        return;
    }

    auto settings = t->getModel().settings();
    settings.keyRoot = static_cast<std::uint8_t>(result.root);
    settings.keyMinor = result.minor ? 1 : 0;
    settings.snapMode = 2; // スケールへのスナップに切り替える
    t->getModel().setSettings(settings);

    keyResult.setText(s.batchKeyResult + kNoteNames[result.root] + " " + (result.minor ? s.scaleMinor : s.scaleMajor)
                          + " (" + juce::String(juce::roundToInt(result.confidence * 100.0f)) + "%)",
                      juce::dontSendNotification);

    if (snapSettingsChanged)
        snapSettingsChanged();

    t->sendChangeMessage();
}

int BatchPanel::getPreferredHeight() const noexcept
{
    return 8 + 24 + 3 * SliderRow::kHeight + 18 + 24 + 8 + 28 + 18 + 24 + 8 + 40 + 36 + 36 + 6 + 36 + 6 + 40 + 16;
}

void BatchPanel::resized()
{
    auto area = getLocalBounds().reduced(10, 8);

    title.setBounds(area.removeFromTop(24));
    pitchStrength.place(area);
    vibratoKeep.place(area);
    timingStrength.place(area);

    gridLabel.setBounds(area.removeFromTop(18));
    gridBox.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);
    keepDuration.setBounds(area.removeFromTop(28));

    bpmLabel.setBounds(area.removeFromTop(18));
    bpmEditor.setBounds(area.removeFromTop(24).removeFromLeft(90));
    area.removeFromTop(4);
    tempoHint.setBounds(area.removeFromTop(40));

    auto row = area.removeFromTop(36);
    applySelected.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2, 0));
    applyAll.setBounds(row.reduced(2, 0));
    area.removeFromTop(6);
    detect.setBounds(area.removeFromTop(36).reduced(2, 0));
    area.removeFromTop(6);
    keyResult.setBounds(area.removeFromTop(40));
}

//==============================================================================
// MIDI
class MidiPanel::DragSource final : public juce::Component
{
public:
    std::function<juce::File()> makeFile;
    juce::String text;

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(0xff2c3340));
        g.fillRoundedRectangle(b, 5.0f);
        g.setColour(juce::Colour(0xfff2b441).withAlpha(0.8f));
        g.drawRoundedRectangle(b, 5.0f, 1.5f);
        g.setColour(juce::Colour(0xffd5dae3));
        g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText(text, getLocalBounds().reduced(8, 2), juce::Justification::centred, 2);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6 || ! makeFile)
            return;

        const auto file = makeFile();
        if (! file.existsAsFile())
            return;

        // ドロップが完了するまで戻らない（外部へのドラッグ&ドロップ）
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.getFullPathName() }, false, this, [this] { dragging = false; });
        dragging = false;
    }

    void mouseUp(const juce::MouseEvent&) override { dragging = false; }

private:
    bool dragging = false;
};

MidiPanel::MidiPanel(PianoRollView& r) : SidePanel(r)
{
    language = detectDefaultLanguage();

    for (auto* l : { &exportTitle, &importTitle })
    {
        styleSectionLabel(*l);
        addAndMakeVisible(*l);
    }

    for (auto* l : { &dragHint, &assignHint, &dropHint, &statusLabel })
    {
        styleHint(*l);
        addAndMakeVisible(*l);
    }

    offsetLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd5dae3));
    offsetLabel.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(offsetLabel);

    message.setColour(juce::Label::textColourId, juce::Colour(0xffe2a08b));
    message.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(message);

    dragSource = std::make_unique<DragSource>();
    dragSource->makeFile = [this] { return makeExportFile(true); };
    addAndMakeVisible(*dragSource);

    styleToggle(withBend);
    styleToggle(edited);
    addAndMakeVisible(withBend);
    addAndMakeVisible(edited);

    styleEditor(offsetEditor);
    offsetEditor.setInputRestrictions(10, "0123456789.-");
    offsetEditor.onReturnKey = [this] { commitOffset(); };
    offsetEditor.onFocusLost = [this] { commitOffset(); };
    addAndMakeVisible(offsetEditor);

    using F = SliderRow::Format;
    pitchStrength.init(*this, 0.0, 1.0, 0.01, 1.0, F::Percent);
    timingStrength.init(*this, 0.0, 1.0, 0.01, 1.0, F::Percent);
    pitchStrength.slider.setValue(1.0, juce::dontSendNotification);
    timingStrength.slider.setValue(1.0, juce::dontSendNotification);

    save.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("MIDI", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("KoeNote.mid"), "*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                             [this] (const juce::FileChooser& fc)
                             {
                                 auto file = fc.getResult();
                                 if (file == juce::File())
                                     return;

                                 if (! file.hasFileExtension("mid"))
                                     file = file.withFileExtension("mid");

                                 message.setText(writeSmfTo(file) ? juce::String() : getStrings(language).midiSaveFailed, juce::dontSendNotification);
                             });
    };

    load.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("MIDI", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.mid;*.midi");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this] (const juce::FileChooser& fc)
                             {
                                 if (const auto file = fc.getResult(); file.existsAsFile())
                                     importFile(file);
                             });
    };

    autoOffset.onClick = [this]
    {
        if (auto* t = target(); t != nullptr && sequence.has_value())
        {
            offsetSec = model::autoMidiOffset(t->getModel().notes(), *sequence);
            offsetEditor.setText(juce::String(offsetSec, 3), false);
            reimport();
        }
    };

    apply_.onClick = [this] { apply(); };
    clear.onClick = [this]
    {
        if (auto* t = target())
        {
            t->getModel().clearTargets();
            sequence.reset();
            t->sendChangeMessage();
        }
    };

    for (auto* b : { &save, &load, &autoOffset, &apply_, &clear })
        addAndMakeVisible(*b);

    updateTexts();
    refresh();
}

MidiPanel::~MidiPanel() = default;

void MidiPanel::setLanguage(Language l)
{
    language = l;
    updateTexts();
    refresh();
}

void MidiPanel::updateTexts()
{
    const auto& s = getStrings(language);
    exportTitle.setText(s.midiExportTitle, juce::dontSendNotification);
    importTitle.setText(s.midiImportTitle, juce::dontSendNotification);
    dragHint.setText(s.midiDragHint, juce::dontSendNotification);
    dropHint.setText(s.midiDropHint, juce::dontSendNotification);
    assignHint.setText(s.midiAssignHint, juce::dontSendNotification);
    offsetLabel.setText(s.midiOffset, juce::dontSendNotification);
    dragSource->text = s.midiDrag;
    dragSource->repaint();
    save.setButtonText(s.midiSave);
    load.setButtonText(s.midiLoad);
    autoOffset.setButtonText(s.midiAutoOffset);
    apply_.setButtonText(s.midiApply);
    clear.setButtonText(s.midiClear);
    withBend.setButtonText(s.midiWithBend);
    edited.setButtonText(s.midiEdited);
    pitchStrength.label.setText(s.midiPitchStrength, juce::dontSendNotification);
    timingStrength.label.setText(s.midiTimingStrength, juce::dontSendNotification);
    updateStatus();
}

void MidiPanel::refresh()
{
    auto* t = target();
    const bool hasModel = t != nullptr && t->getModel().hasAnalysis();

    for (auto* c : std::initializer_list<juce::Component*> { dragSource.get(), &save, &load, &withBend, &edited })
        c->setEnabled(hasModel);

    const bool hasTargets = hasModel && ! t->getModel().targets().empty();
    for (auto* c : std::initializer_list<juce::Component*> { &apply_, &clear, &offsetEditor, &autoOffset })
        c->setEnabled(hasTargets && (c != &offsetEditor && c != &autoOffset ? true : sequence.has_value()));

    pitchStrength.setEnabledState(hasTargets);
    timingStrength.setEnabledState(hasTargets);
    updateStatus();
}

void MidiPanel::updateStatus()
{
    auto* t = target();
    if (t == nullptr || t->getModel().targets().empty())
    {
        statusLabel.setText({}, juce::dontSendNotification);
        return;
    }

    const auto& m = t->getModel();
    int matched = 0;
    for (const auto& n : m.notes())
        if (m.assignmentOf(n.id) >= 0)
            ++matched;

    const int targets = static_cast<int>(m.targets().size());

    auto text = getStrings(language).midiStatus;
    text = text.replace("%t", juce::String(targets)).replace("%p", juce::String(matched))
               .replace("%a", juce::String(static_cast<int>(m.notes().size()) - matched)).replace("%m", juce::String(targets - matched));
    statusLabel.setText(text, juce::dontSendNotification);
}

//------------------------------------------------------------------------------
bool MidiPanel::writeSmfTo(const juce::File& file)
{
    auto* t = target();
    if (t == nullptr || ! t->getModel().hasAnalysis())
        return false;

    const auto& m = t->getModel();
    const bool useEdited = edited.getToggleState();
    const auto notes = model::notesToMidi(m.notes(), useEdited);

    if (notes.empty())
    {
        message.setText(getStrings(language).midiNoNotes, juce::dontSendNotification);
        return false;
    }

    midi::SmfWriteOptions options;

    // ホストのテンポがあれば、クリップの位置でのテンポとその後の変化を書き込む（DAW のグリッドに揃えるため）
    const auto tempo = t->getTempoMap();
    if (tempo.valid())
    {
        const double offset = t->getTimeOffsetSec();
        options.tempos.push_back({ 0.0, std::clamp(tempo.bpmAt(offset), 20.0, 400.0) });

        // クリップの位置の拍子
        const double startQuarter = tempo.secToQuarter(offset);
        for (const auto& s : tempo.barSignatures())
        {
            if (s.quarter > startQuarter + 1.0e-6 && &s != &tempo.barSignatures().front())
                break;

            options.timeSignatureNumerator = s.numerator;
            options.timeSignatureDenominator = s.denominator;
        }

        const auto& entries = tempo.entries();
        for (std::size_t i = 0; i + 1 < entries.size(); ++i)
        {
            if (entries[i].sec - offset > 0.0)
            {
                const double bpm = (entries[i + 1].quarter - entries[i].quarter) / (entries[i + 1].sec - entries[i].sec) * 60.0;
                options.tempos.push_back({ entries[i].sec - offset, std::clamp(bpm, 20.0, 400.0) });
            }
        }
    }

    if (withBend.getToggleState() && ! useEdited)
        for (const auto& p : model::pitchBendCurve(m.pitchTrack(), m.notes()))
            options.pitchBend.push_back(p);

    const auto bytes = midi::writeSmf(notes, options);
    return file.replaceWithData(bytes.data(), bytes.size());
}

juce::File MidiPanel::makeExportFile(bool forDrag)
{
    (void) forDrag;
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("KoeNote");
    dir.createDirectory();
    const auto file = dir.getChildFile("KoeNote_" + juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S") + ".mid");

    if (! writeSmfTo(file))
        return {};

    message.setText({}, juce::dontSendNotification);
    return file;
}

bool MidiPanel::importFile(const juce::File& file)
{
    auto* t = target();
    if (t == nullptr || ! t->getModel().hasAnalysis())
        return false;

    juce::MemoryBlock data;
    if (! file.loadFileAsData(data) || data.getSize() == 0)
    {
        message.setText(getStrings(language).midiLoadFailed, juce::dontSendNotification);
        return false;
    }

    auto parsed = midi::readSmf(static_cast<const std::uint8_t*>(data.getData()), data.getSize());

    if (! parsed.has_value() || parsed->notes.empty())
    {
        message.setText(getStrings(language).midiLoadFailed, juce::dontSendNotification);
        return false;
    }

    message.setText({}, juce::dontSendNotification);
    sequence = std::move(parsed);
    offsetSec = model::autoMidiOffset(t->getModel().notes(), *sequence);
    offsetEditor.setText(juce::String(offsetSec, 3), false);
    reimport();
    return true;
}

void MidiPanel::commitOffset()
{
    if (updating || ! sequence.has_value())
        return;

    offsetSec = parseNumber(offsetEditor.getText(), offsetSec);
    reimport();
}

void MidiPanel::reimport()
{
    auto* t = target();
    if (t == nullptr || ! sequence.has_value())
        return;

    const auto imported = model::importMidi(t->getModel().notes(), *sequence, offsetSec);
    t->getModel().setTargets(imported.targets, imported.assignments);
    t->sendChangeMessage();
    roll.refreshView();
}

void MidiPanel::apply()
{
    auto* t = target();
    if (t == nullptr)
        return;

    const auto edits = model::computeTargetEdits(t->getModel(), static_cast<float>(pitchStrength.slider.getValue()),
                                                 static_cast<float>(timingStrength.slider.getValue()));

    if (t->getModel().applyEdits(edits))
        t->notifyEdited();
}

bool MidiPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::any_of(files.begin(), files.end(), [] (const juce::String& f) { return f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi"); });
}

void MidiPanel::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi"))
        {
            importFile(juce::File(f));
            return;
        }
}

int MidiPanel::getPreferredHeight() const noexcept
{
    return 8 + 24 + 40 + 34 + 28 * 2 + 36 + 14 + 24 + 36 + 6 + 20 + 36 + 2 * SliderRow::kHeight + 36 + 6 + 40 + 56 + 40 + 20;
}

void MidiPanel::resized()
{
    auto area = getLocalBounds().reduced(10, 8);

    exportTitle.setBounds(area.removeFromTop(24));
    dragSource->setBounds(area.removeFromTop(40));
    area.removeFromTop(4);
    dragHint.setBounds(area.removeFromTop(34));
    withBend.setBounds(area.removeFromTop(28));
    edited.setBounds(area.removeFromTop(28));
    save.setBounds(area.removeFromTop(36).removeFromLeft(120).reduced(2, 0));
    area.removeFromTop(14);

    importTitle.setBounds(area.removeFromTop(24));
    load.setBounds(area.removeFromTop(36).removeFromLeft(120).reduced(2, 0));
    area.removeFromTop(6);
    message.setBounds(area.removeFromTop(20));

    offsetLabel.setBounds(area.removeFromTop(18));
    auto row = area.removeFromTop(28);
    offsetEditor.setBounds(row.removeFromLeft(90));
    row.removeFromLeft(6);
    autoOffset.setBounds(row.removeFromLeft(120));
    area.removeFromTop(8);

    pitchStrength.place(area);
    timingStrength.place(area);

    auto buttons = area.removeFromTop(36);
    apply_.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).reduced(2, 0));
    clear.setBounds(buttons.reduced(2, 0));
    area.removeFromTop(6);

    statusLabel.setBounds(area.removeFromTop(40));
    assignHint.setBounds(area.removeFromTop(56));
    dropHint.setBounds(area.removeFromTop(40));
}

//==============================================================================
// A/B・整列・ニューラル
ToolsPanel::ToolsPanel(PianoRollView& r) : SidePanel(r)
{
    language = detectDefaultLanguage();

    for (auto* l : { &compareTitle, &alignTitle, &neuralTitle, &rangeTitle })
    {
        styleSectionLabel(*l);
        addAndMakeVisible(*l);
    }

    for (auto* l : { &alignStatus, &neuralStatus, &neuralHint, &offsetHint, &rangeStatus })
    {
        styleHint(*l);
        addAndMakeVisible(*l);
    }

    offsetLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd5dae3));
    offsetLabel.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(offsetLabel);

    styleEditor(offsetEditor);
    offsetEditor.setInputRestrictions(10, "0123456789.-");
    offsetEditor.setText("0.000", false);
    offsetEditor.onReturnKey = [this] { commitGuideOffset(); };
    offsetEditor.onFocusLost = [this] { commitGuideOffset(); };
    addAndMakeVisible(offsetEditor);

    guideBox.onChange = [this]
    {
        if (auto* t = target(); t != nullptr && guideBox.getSelectedId() > 0)
            t->selectGuide(guideBox.getSelectedId() - 1);
    };

    offsetMinus.setButtonText("-10 ms");
    offsetPlus.setButtonText("+10 ms");
    offsetMinus.onClick = [this] { nudgeOffset(-0.010); };
    offsetPlus.onClick = [this] { nudgeOffset(0.010); };
    guideClear.onClick = [this] { clearGuide(); };
    rangeClear.onClick = [this] { roll.clearTimingRange(); };
    addAndMakeVisible(rangeClear);
    for (auto* b : { &offsetMinus, &offsetPlus, &guideClear })
        addAndMakeVisible(*b);

    alignGuideLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd5dae3));
    alignGuideLabel.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(alignGuideLabel);

    for (auto* b : { &compareOriginal, &compareRange, &neuralEnable })
    {
        styleToggle(*b);
        addAndMakeVisible(*b);
    }

    compareOriginal.onClick = [this] { applyCompare(); };
    compareRange.onClick = [this] { applyCompare(); };
    neuralEnable.onClick = [this] { toggleNeural(); };
    alignRun.onClick = [this] { runAlign(); };
    alignFile.onClick = [this] { alignFromFile(); };

    addAndMakeVisible(guideBox);
    addAndMakeVisible(alignRun);
    addAndMakeVisible(alignFile);

    updateTexts();
    refresh();
}

void ToolsPanel::setLanguage(Language l)
{
    language = l;
    updateTexts();
    lastGuideCount = -1;
    refresh();
}

void ToolsPanel::updateTexts()
{
    const auto& s = getStrings(language);
    compareTitle.setText(s.compareTitle, juce::dontSendNotification);
    compareOriginal.setButtonText(s.compareOriginal);
    compareRange.setButtonText(s.compareRange);
    alignTitle.setText(s.alignTitle, juce::dontSendNotification);
    rangeTitle.setText(s.rangeTitle, juce::dontSendNotification);
    rangeClear.setButtonText(s.rangeClear);
    alignGuideLabel.setText(s.alignGuide, juce::dontSendNotification);
    alignRun.setButtonText(s.alignRun);
    alignFile.setButtonText(s.alignFile);
    offsetLabel.setText(s.alignOffset, juce::dontSendNotification);
    guideClear.setButtonText(s.alignOffsetOnly);
    offsetHint.setText(s.alignOffsetHint, juce::dontSendNotification);
    neuralTitle.setText(s.neuralTitle, juce::dontSendNotification);
    neuralEnable.setButtonText(s.neuralEnable);

    const auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("KoeNote").getChildFile("neural");
    neuralHint.setText(s.neuralFolderHint + dir.getFullPathName(), juce::dontSendNotification);
}

void ToolsPanel::setCompareOriginal(bool original)
{
    compareOriginal.setToggleState(original, juce::dontSendNotification);
    applyCompare();
}

bool ToolsPanel::isCompareOriginal() const
{
    return compareOriginal.getToggleState();
}

void ToolsPanel::applyCompare()
{
    if (updating)
        return;

    auto* t = target();
    if (t == nullptr)
        return;

    plugin::CompareState state;
    state.useOriginal = compareOriginal.getToggleState();

    double start = 0.0, end = 0.0;
    state.rangeOnly = compareRange.getToggleState() && roll.getSelectionRange(start, end);
    state.startSec = start;
    state.endSec = end;
    t->setCompare(state);
}

void ToolsPanel::refreshGuides()
{
    auto* t = target();
    const auto guides = t != nullptr ? t->getGuideCandidates() : std::vector<plugin::GuideCandidate> {};

    if (static_cast<int>(guides.size()) == lastGuideCount)
        return;

    lastGuideCount = static_cast<int>(guides.size());
    const int previous = guideBox.getSelectedId();

    guideBox.clear(juce::dontSendNotification);
    for (const auto& g : guides)
        guideBox.addItem(g.name, g.id + 1);

    if (guideBox.indexOfItemId(previous) >= 0)
        guideBox.setSelectedId(previous, juce::dontSendNotification);
    else
        guideBox.setTextWhenNothingSelected(guides.empty() ? getStrings(language).alignNoGuide : getStrings(language).guideShow);
}

double ToolsPanel::currentOffset() const
{
    const auto* t = target();
    return t != nullptr ? t->getGuide().offsetSec : 0.0;
}

void ToolsPanel::nudgeOffset(double deltaSec)
{
    if (auto* t = target(); t != nullptr && t->getGuide().active)
        t->setGuideOffset(currentOffset() + deltaSec);
}

void ToolsPanel::commitGuideOffset()
{
    auto* t = target();
    if (t == nullptr || updating || ! t->getGuide().active)
        return;

    const auto text = offsetEditor.getText().trim();
    if (text.isNotEmpty() && text.containsOnly("0123456789.-+"))
        t->setGuideOffset(text.getDoubleValue());
    else
        offsetEditor.setText(juce::String(currentOffset(), 3), false);
}

void ToolsPanel::clearGuide()
{
    if (auto* t = target())
    {
        t->clearGuide();
        guideBox.setSelectedId(0, juce::dontSendNotification);
    }
}

void ToolsPanel::runAlign()
{
    if (auto* t = target(); t != nullptr && t->getGuide().analysis != nullptr)
        t->alignToGuide();
}

void ToolsPanel::alignFromFile()
{
    auto* t = target();
    if (t == nullptr)
        return;

    chooser = std::make_unique<juce::FileChooser>(getStrings(language).alignGuide, juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                                  "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this] (const juce::FileChooser& fc)
                         {
                             auto* target_ = target();
                             const auto file = fc.getResult();
                             if (target_ == nullptr || ! file.existsAsFile())
                                 return;

                             juce::AudioFormatManager formats;
                             formats.registerBasicFormats();
                             std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));

                             if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > (1LL << 31))
                                 return;

                             auto data = std::make_shared<plugin::SourceAudioData>();
                             data->numChannels = static_cast<int>(std::min<unsigned int>(reader->numChannels, 2));
                             data->numFrames = reader->lengthInSamples;
                             data->sampleRate = reader->sampleRate;
                             juce::AudioBuffer<float> buffer(data->numChannels, static_cast<int>(data->numFrames));
                             reader->read(&buffer, 0, static_cast<int>(data->numFrames), 0, true, data->numChannels > 1);

                             for (int c = 0; c < data->numChannels; ++c)
                                 data->channels.emplace_back(buffer.getReadPointer(c), buffer.getReadPointer(c) + data->numFrames);
                             for (const auto& channel : data->channels)
                                 data->channelPointers.push_back(channel.data());

                             target_->selectGuideAudio(std::move(data), file.getFileName());
                             guideBox.setSelectedId(0, juce::dontSendNotification);
                         });
}

void ToolsPanel::toggleNeural()
{
    auto* t = target();
    if (t == nullptr || updating)
        return;

    auto s = t->getModel().settings();
    s.renderMode = neuralEnable.getToggleState() ? 1 : 0;
    t->getModel().setSettings(s);
    t->notifyEdited();
}

void ToolsPanel::refresh()
{
    auto* t = target();
    const juce::ScopedValueSetter<bool> guard(updating, true);
    const auto& s = getStrings(language);

    const bool hasModel = t != nullptr && t->getModel().hasAnalysis();
    for (auto* c : std::initializer_list<juce::Component*> { &compareOriginal, &compareRange, &guideBox, &alignRun, &alignFile, &neuralEnable })
        c->setEnabled(hasModel);

    if (! hasModel)
        return;

    refreshGuides();

    {
        double a = 0.0, b = 0.0;
        const bool ranged = roll.getTimingRange(a, b);
        rangeClear.setEnabled(ranged);
        rangeStatus.setText(ranged ? s.rangeStatus + juce::String(a, 2) + " - " + juce::String(b, 2) + " s\n" + s.rangeHint : s.rangeNone,
                            juce::dontSendNotification);
    }

    // ガイドの状態に合わせる（ずれの欄は、編集中でなければ現在値に追従させる）
    const auto guide = t->getGuide();
    for (auto* c : std::initializer_list<juce::Component*> { &offsetEditor, &offsetMinus, &offsetPlus, &guideClear })
        c->setEnabled(guide.active);

    alignRun.setEnabled(guide.analysis != nullptr);

    if (! offsetEditor.hasKeyboardFocus(true))
        offsetEditor.setText(juce::String(guide.offsetSec, 3), false);

    const auto compare = t->getCompare();
    compareOriginal.setToggleState(compare.useOriginal, juce::dontSendNotification);
    compareRange.setToggleState(compare.rangeOnly || compareRange.getToggleState(), juce::dontSendNotification);

    // A/B の範囲は、選択ノートの範囲に追従させる
    if (compare.useOriginal && compareRange.getToggleState())
    {
        double a = 0.0, b = 0.0;
        if (roll.getSelectionRange(a, b) && (std::abs(a - compare.startSec) > 1.0e-6 || std::abs(b - compare.endSec) > 1.0e-6 || ! compare.rangeOnly))
        {
            plugin::CompareState next = compare;
            next.rangeOnly = true;
            next.startSec = a;
            next.endSec = b;
            t->setCompare(next);
        }
    }

    const auto align = t->getAlignInfo();
    switch (align.state)
    {
        case plugin::AlignInfo::State::Working: alignStatus.setText(s.alignWorking + " " + juce::String(juce::roundToInt(align.progress * 100.0f)) + "%", juce::dontSendNotification); break;
        case plugin::AlignInfo::State::Done: alignStatus.setText(s.alignDone, juce::dontSendNotification); break;
        case plugin::AlignInfo::State::Failed: alignStatus.setText(s.alignFailed, juce::dontSendNotification); break;
        case plugin::AlignInfo::State::Idle: alignStatus.setText({}, juce::dontSendNotification); break;
    }

    const auto neural = t->getNeuralInfo();
    const bool enabled = t->getModel().settings().renderMode == 1;
    neuralEnable.setToggleState(enabled, juce::dontSendNotification);
    neuralEnable.setEnabled(neural.state != plugin::NeuralInfo::State::Unavailable || enabled);

    switch (neural.state)
    {
        case plugin::NeuralInfo::State::Unavailable: neuralStatus.setText(s.neuralUnavailable + neural.message, juce::dontSendNotification); break;
        case plugin::NeuralInfo::State::Idle: neuralStatus.setText(enabled ? s.neuralWaiting : juce::String(), juce::dontSendNotification); break;
        case plugin::NeuralInfo::State::Rendering:
            neuralStatus.setText(s.neuralRendering + " " + juce::String(juce::roundToInt(neural.progress * 100.0f)) + "%", juce::dontSendNotification);
            break;
        case plugin::NeuralInfo::State::Ready: neuralStatus.setText(s.neuralReady, juce::dontSendNotification); break;
        case plugin::NeuralInfo::State::Failed: neuralStatus.setText(s.neuralFailed + neural.message, juce::dontSendNotification); break;
    }
}

int ToolsPanel::getPreferredHeight() const noexcept
{
    return 8 + 24 + 28 * 2 + 14 + (24 + 86 + 30 + 14) + 24 + 18 + 24 + 8 + 36 + 22 + 18 + 28 + 6 + 36 + 6 + 110 + 14 + 24 + 28 + 56 + 80 + 16;
}

void ToolsPanel::resized()
{
    auto area = getLocalBounds().reduced(10, 8);

    compareTitle.setBounds(area.removeFromTop(24));
    compareOriginal.setBounds(area.removeFromTop(28));
    compareRange.setBounds(area.removeFromTop(28));
    area.removeFromTop(14);

    rangeTitle.setBounds(area.removeFromTop(24));
    rangeStatus.setBounds(area.removeFromTop(86));
    rangeClear.setBounds(area.removeFromTop(30).removeFromLeft(150).reduced(2, 0));
    area.removeFromTop(14);

    alignTitle.setBounds(area.removeFromTop(24));
    alignGuideLabel.setBounds(area.removeFromTop(18));
    guideBox.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);
    auto row = area.removeFromTop(36);
    alignRun.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2, 0));
    alignFile.setBounds(row.reduced(2, 0));
    alignStatus.setBounds(area.removeFromTop(22));

    offsetLabel.setBounds(area.removeFromTop(18));
    auto offsetRow = area.removeFromTop(28);
    offsetEditor.setBounds(offsetRow.removeFromLeft(80));
    offsetRow.removeFromLeft(6);
    offsetMinus.setBounds(offsetRow.removeFromLeft(70));
    offsetRow.removeFromLeft(4);
    offsetPlus.setBounds(offsetRow.removeFromLeft(70));
    area.removeFromTop(6);
    guideClear.setBounds(area.removeFromTop(36).removeFromLeft(150).reduced(2, 0));
    area.removeFromTop(6);
    offsetHint.setBounds(area.removeFromTop(110));
    area.removeFromTop(14);

    neuralTitle.setBounds(area.removeFromTop(24));
    neuralEnable.setBounds(area.removeFromTop(28));
    neuralStatus.setBounds(area.removeFromTop(56));
    neuralHint.setBounds(area.removeFromTop(80));
}
} // namespace koenote::ui
