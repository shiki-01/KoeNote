#pragma once

#include "model/Note.h"
#include "plugin/EditTarget.h"
#include "render/TimeMap.h"
#include "ui/Strings.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <set>
#include <vector>

namespace koenote::ui
{
enum class Tool
{
    Select,
    Split,
    Boundary
};

enum class SnapMode
{
    Off,
    Semitone,
    Scale
};

enum class ScaleType
{
    Major,
    Minor
};

// 音名に対するスケールの構成音（半音単位、ルートからの距離）
bool isInScale(int pitchClass, int root, ScaleType scale) noexcept;

// midi を指定のスナップ設定で最も近い音へ丸める
float snapMidi(float midi, SnapMode mode, int root, ScaleType scale) noexcept;

// 波形・ノート・ピッチ曲線を表示し、ノートを編集するピアノロール（要件 F-03〜F-07）。
// 表示する時間軸は出力時間軸（編集後）。すべて message thread で動作する。
class PianoRollView final : public juce::Component,
                            private juce::ChangeListener,
                            private plugin::EditTarget::DestructionListener,
                            private juce::Timer
{
public:
    PianoRollView();
    ~PianoRollView() override;

    void setModification(plugin::EditTarget* modification);
    plugin::EditTarget* getModification() const noexcept { return current; }

    void setLanguage(Language l);
    void setTool(Tool t);
    Tool getTool() const noexcept { return tool; }
    void setSnap(SnapMode mode, int rootPitchClass, ScaleType scale);

    // 再生位置（出力時間軸の秒）。負の値で非表示。
    void setPlayhead(double seconds);

    const std::set<model::NoteId>& getSelection() const noexcept { return selection; }
    std::function<void()> onSelectionChanged;
    std::function<void()> onModelChanged; // 編集・Undo などでモデルが変わったとき

    //==========================================================================
    void undo();
    void redo();
    void mergeSelected();
    void resetSelected();
    void selectAll();
    void zoomToFit();

    // 選択中のノートの編集パラメータを変更する（スライダー操作用）。
    // begin/end でドラッグ全体を 1 回の Undo にまとめる。
    void beginParameterEdit();
    void applyToSelection(const std::function<void(model::NoteEdit&)>& change);
    void endParameterEdit();

    //==========================================================================
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    enum class DragKind
    {
        None,
        Pitch,
        Time,
        ResizeStart,
        ResizeEnd,
        Boundary,
        Rubber,
        Pan
    };

    struct DragState
    {
        DragKind kind = DragKind::None;
        juce::Point<int> startPoint;
        model::NoteId primary = 0;
        std::vector<model::Note> startNotes; // ドラッグ開始時の選択ノート
        double startViewSec = 0.0;
        double startTopMidi = 0.0;
        juce::Rectangle<int> rubber;
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void editTargetWillBeDestroyed(plugin::EditTarget* target) override;
    void timerCallback() override;

    // 座標変換
    juce::Rectangle<int> getRollArea() const;
    juce::Rectangle<int> getWaveformArea() const;
    juce::Rectangle<int> getRulerArea() const;
    juce::Rectangle<int> getKeyboardArea() const;
    float xOfSec(double sec) const;
    double secOfX(float x) const;
    float yOfMidi(double midi) const;
    double midiOfY(float y) const;
    juce::Rectangle<float> noteBounds(const model::Note& n) const;

    // 描画
    void drawGrid(juce::Graphics& g, juce::Rectangle<int> area) const;
    void drawKeyboard(juce::Graphics& g) const;
    void drawRuler(juce::Graphics& g) const;
    void drawNotes(juce::Graphics& g) const;
    void drawCurve(juce::Graphics& g) const;
    void drawWaveform(juce::Graphics& g) const;
    void drawPlayhead(juce::Graphics& g) const;
    void drawMessage(juce::Graphics& g, const juce::String& text) const;

    // モデル
    void refreshCaches();
    void rebuildPeaks();
    void pruneSelection();
    void setSelection(std::set<model::NoteId> ids);
    void notifyModelEdited(bool immediately);
    model::EditModel* model() const;
    const model::Note* hitTestNote(juce::Point<float> p, DragKind* edge) const;
    bool hitTestBoundary(juce::Point<float> p, model::NoteId* leftId) const;
    void clampView();
    void applyDrag(const juce::MouseEvent& e);

    plugin::EditTarget* current = nullptr;
    Language language = Language::Japanese;
    Tool tool = Tool::Select;
    SnapMode snapMode = SnapMode::Semitone;
    int snapRoot = 0;
    ScaleType snapScale = ScaleType::Major;

    // 表示範囲
    double viewStartSec = 0.0;
    double pixelsPerSec = 100.0;
    double topMidi = 84.0;
    double pixelsPerSemitone = 14.0;

    std::set<model::NoteId> selection;
    DragState drag;
    double playheadSec = -1.0;
    double hoverSec = 0.0;
    bool pendingNotify = false;
    bool editGestureOpen = false;

    // 描画用キャッシュ（revision が変わったときに更新）
    std::uint64_t cachedRevision = 0;
    bool cacheValid = false;
    render::TimeMap timeMap;
    struct CurvePoint
    {
        double outSec;
        double midi;
        bool breakBefore;
    };
    std::vector<CurvePoint> curve;

    std::shared_ptr<const plugin::SourceAudioData> peaksSource;
    std::vector<float> peakMin, peakMax;
    static constexpr int kPeakBlock = 256;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollView)
};
} // namespace koenote::ui
