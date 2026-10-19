#pragma once

#include "midi/SmfIO.h"
#include "ui/PianoRollView.h"
#include "ui/SliderRow.h"
#include "ui/Strings.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>

namespace koenote::ui
{
// サイドパネルの共通の振る舞い
class SidePanel : public juce::Component
{
public:
    explicit SidePanel(PianoRollView& rollIn) : roll(rollIn) {}

    virtual void setLanguage(Language l) = 0;
    // 選択やモデルが変わったとき、または定期的に呼ぶ
    virtual void refresh() = 0;
    // 全項目を表示するのに必要な高さ
    virtual int getPreferredHeight() const noexcept = 0;

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff20232a)); }

protected:
    plugin::EditTarget* target() const { return roll.getModification(); }

    PianoRollView& roll;
    Language language = Language::Japanese;
};

//==============================================================================
// 一括補正（F-16）、グリッド・テンポ（F-14）、キー・スケール検出（F-15）
class BatchPanel final : public SidePanel
{
public:
    // onSnapSettingsChanged: キー検出などでスナップ設定が変わったときに、ツールバーの表示を更新してもらう
    BatchPanel(PianoRollView& roll, std::function<void()> onSnapSettingsChanged);

    void setLanguage(Language l) override;
    void refresh() override;
    int getPreferredHeight() const noexcept override;
    void resized() override;

private:
    void apply(bool selectedOnly);
    void detectKey();
    void commitBpm();
    void updateTexts();

    std::function<void()> snapSettingsChanged;
    bool updating = false;

    juce::Label title, gridLabel, bpmLabel, keyResult, tempoHint;
    SliderRow pitchStrength, vibratoKeep, timingStrength;
    juce::ComboBox gridBox;
    juce::ToggleButton keepDuration;
    juce::TextEditor bpmEditor;
    juce::TextButton applySelected, applyAll, detect;
};

//==============================================================================
// MIDI の抽出（F-11）と、MIDI をターゲットに適用（F-12）
class MidiPanel final : public SidePanel,
                        public juce::FileDragAndDropTarget
{
public:
    explicit MidiPanel(PianoRollView& roll);
    ~MidiPanel() override;

    void setLanguage(Language l) override;
    void refresh() override;
    int getPreferredHeight() const noexcept override;
    void resized() override;

    // MIDI ファイルを目標として読み込む（ファイルのドロップ・選択から）。成功すれば true。
    bool importFile(const juce::File& file);

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    class DragSource;

    juce::File makeExportFile(bool forDrag);
    bool writeSmfTo(const juce::File& file);
    void reimport();
    void apply();
    void updateStatus();
    void updateTexts();
    void commitOffset();

    std::optional<midi::MidiSequence> sequence;
    double offsetSec = 0.0;
    bool updating = false;

    juce::Label exportTitle, importTitle, dragHint, statusLabel, assignHint, dropHint, offsetLabel, message;
    std::unique_ptr<DragSource> dragSource;
    juce::TextButton save, load, autoOffset, apply_, clear;
    juce::ToggleButton withBend, edited;
    juce::TextEditor offsetEditor;
    SliderRow pitchStrength, timingStrength;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
// A/B 比較（F-18）、タイミング整列（F-13）、ニューラル加工（F-17）
class ToolsPanel final : public SidePanel
{
public:
    explicit ToolsPanel(PianoRollView& roll);

    void setLanguage(Language l) override;
    void refresh() override;
    int getPreferredHeight() const noexcept override;
    void resized() override;

    // ツールバーの A/B ボタンからも使う
    void setCompareOriginal(bool original);
    bool isCompareOriginal() const;

private:
    void applyCompare();
    void runAlign();
    void alignFromFile();
    void clearGuide();
    void commitGuideOffset();
    void nudgeOffset(double deltaSec);
    double currentOffset() const;
    void refreshGuides();
    void toggleNeural();
    void updateTexts();

    juce::Label compareTitle, alignTitle, alignGuideLabel, alignStatus, neuralTitle, neuralStatus, neuralHint, offsetLabel, offsetHint;
    juce::TextEditor offsetEditor;
    juce::TextButton offsetMinus, offsetPlus, guideClear;
    juce::ToggleButton compareOriginal, compareRange, neuralEnable;
    juce::ComboBox guideBox;
    juce::TextButton alignRun, alignFile;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastGuideCount = -1;
    bool updating = false;
};
} // namespace koenote::ui
