#include "ui/NoteParamPanel.h"

#include <cmath>

namespace koenote::ui
{
NoteParamPanel::NoteParamPanel(PianoRollView& view) : roll(view)
{
    language = detectDefaultLanguage();

    using F = SliderRow::Format;
    setupRow(center, 0.0, 1.0, 0.01, 0.0, F::Percent, [] (model::NoteEdit& e, float v) { e.centerCorrection = v; });
    setupRow(vibrato, 0.0, 2.0, 0.01, 1.0, F::Percent, [] (model::NoteEdit& e, float v) { e.vibratoScale = v; });
    setupRow(drift, 0.0, 2.0, 0.01, 1.0, F::Percent, [] (model::NoteEdit& e, float v) { e.driftScale = v; });
    setupRow(transition, 0.0, 1.0, 0.01, 0.0, F::Percent, [] (model::NoteEdit& e, float v) { e.transitionSmoothness = v; });

    // フォルマントは ±600 セントまで（TD-PSOLA で自然に聞こえる範囲）
    setupRow(formant, -600.0, 600.0, 1.0, 0.0, F::Cents, [] (model::NoteEdit& e, float v) { e.formantShiftCents = v; });
    setupRow(formantFollow, 0.0, 1.0, 0.01, 0.0, F::Percent, [] (model::NoteEdit& e, float v) { e.formantFollow = v; });

    setupRow(gain, -24.0, 12.0, 0.1, 0.0, F::Db, [] (model::NoteEdit& e, float v) { e.gainDb = v; });
    setupRow(consonant, -24.0, 12.0, 0.1, 0.0, F::Db, [] (model::NoteEdit& e, float v) { e.consonantGainDb = v; });
    setupRow(breath, -24.0, 12.0, 0.1, 0.0, F::Db, [] (model::NoteEdit& e, float v) { e.breathGainDb = v; });

    for (auto* l : { &pitchSection, &formantSection, &gainSection })
    {
        styleSectionLabel(*l);
        addAndMakeVisible(*l);
    }

    pitchInfo.setColour(juce::Label::textColourId, juce::Colour(0xffd5dae3));
    pitchInfo.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(pitchInfo);

    emptyHint.setColour(juce::Label::textColourId, juce::Colour(0xff8a93a3));
    emptyHint.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(emptyHint);

    updateTexts();
    refresh();
}

void NoteParamPanel::setupRow(SliderRow& row, double minimum, double maximum, double step, double resetValue, SliderRow::Format format,
                              std::function<void(model::NoteEdit&, float)> setter)
{
    row.init(*this, minimum, maximum, step, resetValue, format);

    row.slider.onDragStart = [this] { roll.beginParameterEdit(); };
    row.slider.onDragEnd = [this] { roll.endParameterEdit(); };
    row.slider.onValueChange = [this, setter, &row]
    {
        if (updating)
            return;

        const auto value = static_cast<float>(row.slider.getValue());
        roll.applyToSelection([&] (model::NoteEdit& e) { setter(e, value); });
    };
}

void NoteParamPanel::setLanguage(Language l)
{
    language = l;
    updateTexts();
    refresh();
}

void NoteParamPanel::updateTexts()
{
    const auto& s = getStrings(language);
    center.label.setText(s.paramCenter, juce::dontSendNotification);
    vibrato.label.setText(s.paramVibrato, juce::dontSendNotification);
    drift.label.setText(s.paramDrift, juce::dontSendNotification);
    transition.label.setText(s.paramTransition, juce::dontSendNotification);
    formant.label.setText(s.paramFormant, juce::dontSendNotification);
    formantFollow.label.setText(s.paramFormantFollow, juce::dontSendNotification);
    gain.label.setText(s.paramGain, juce::dontSendNotification);
    consonant.label.setText(s.paramConsonant, juce::dontSendNotification);
    breath.label.setText(s.paramBreath, juce::dontSendNotification);
    emptyHint.setText(s.paramNone, juce::dontSendNotification);

    pitchSection.setText(language == Language::Japanese ? juce::String::fromUTF8("ピッチ") : juce::String("Pitch"), juce::dontSendNotification);
    formantSection.setText(language == Language::Japanese ? juce::String::fromUTF8("フォルマント") : juce::String("Formant"), juce::dontSendNotification);
    gainSection.setText(language == Language::Japanese ? juce::String::fromUTF8("音量") : juce::String("Level"), juce::dontSendNotification);
}

void NoteParamPanel::refresh()
{
    const model::Note* first = nullptr;

    if (auto* modification = roll.getModification())
        if (! roll.getSelection().empty())
            first = modification->getModel().findNote(*roll.getSelection().begin());

    const bool hasNote = first != nullptr;

    const juce::ScopedValueSetter<bool> guard(updating, true);

    for (auto* row : { &center, &vibrato, &drift, &transition, &formant, &formantFollow, &gain, &consonant, &breath })
        row->setEnabledState(hasNote);

    emptyHint.setVisible(! hasNote);
    pitchInfo.setVisible(hasNote);

    if (hasNote)
    {
        center.slider.setValue(first->edit.centerCorrection, juce::dontSendNotification);
        vibrato.slider.setValue(first->edit.vibratoScale, juce::dontSendNotification);
        drift.slider.setValue(first->edit.driftScale, juce::dontSendNotification);
        transition.slider.setValue(first->edit.transitionSmoothness, juce::dontSendNotification);
        formant.slider.setValue(first->edit.formantShiftCents, juce::dontSendNotification);
        formantFollow.slider.setValue(first->edit.formantFollow, juce::dontSendNotification);
        gain.slider.setValue(first->edit.gainDb, juce::dontSendNotification);
        consonant.slider.setValue(first->edit.consonantGainDb, juce::dontSendNotification);
        breath.slider.setValue(first->edit.breathGainDb, juce::dontSendNotification);

        const float semitones = first->edit.pitchOffsetCents / 100.0f;
        pitchInfo.setText(getStrings(language).paramPitch + ": " + (semitones >= 0.0f ? "+" : "") + juce::String(semitones, 2) + " st",
                          juce::dontSendNotification);
    }
}

int NoteParamPanel::getPreferredHeight() const noexcept
{
    // ピッチ情報 + 見出し 3 + スライダー 9 行
    return 8 + 22 + 6 + 3 * 24 + 9 * SliderRow::kHeight + 20;
}

void NoteParamPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff20232a));
}

void NoteParamPanel::resized()
{
    auto area = getLocalBounds().reduced(10, 8);

    pitchInfo.setBounds(area.removeFromTop(22));
    emptyHint.setBounds(pitchInfo.getBounds());
    area.removeFromTop(6);

    pitchSection.setBounds(area.removeFromTop(24));
    for (auto* row : { &center, &vibrato, &drift, &transition })
        row->place(area);

    formantSection.setBounds(area.removeFromTop(24));
    for (auto* row : { &formant, &formantFollow })
        row->place(area);

    gainSection.setBounds(area.removeFromTop(24));
    for (auto* row : { &gain, &consonant, &breath })
        row->place(area);
}
} // namespace koenote::ui
