#pragma once

#include "model/Note.h"
#include "model/Scale.h"
#include "plugin/EditTarget.h"
#include "render/TimeMap.h"
#include "ui/Strings.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace koenote::ui
{
enum class Tool
{
    Select,
    Split,
    Boundary,
    Assign, // MIDI の目標ノートへの手動の割り当て（F-12）
    Guide,  // 背景のガイド（リードボーカル）の開始位置をドラッグでずらす
    Range,  // タイミング編集の範囲を指定する（範囲の外は動かさない）
    Draw,   // ピッチ曲線を手で描く（F-22）
    Erase   // 手描きのピッチ曲線を消す（F-22）
};

using model::ScaleType;
using model::SnapMode;
using model::isInScale;
using model::snapMidi;

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

    // 再生位置（出力時間軸の秒）。負の値で再生中でないことを表す（直前の位置にカーソルを残す）。
    void setPlayhead(double seconds);
    // 再生位置の変更をホストへ依頼する（定規のクリック・ドラッグ、再生位置の線のドラッグ）。引数は出力時間軸の秒。
    std::function<void(double)> onSeek;

    // タイミング編集の範囲（範囲の外は動かさない）
    bool hasTimingRange() const;
    void clearTimingRange();
    bool getTimingRange(double& startSec, double& endSec) const;

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
    // 出力時間軸の [startSec, endSec] が横幅に収まるように表示する（ホストの選択範囲への追従）
    void showTimeRange(double startSec, double endSec);

    // 選択ノートの範囲（出力時間軸の秒）。選択がなければ false。A/B 比較の範囲に使う。
    bool getSelectionRange(double& startSec, double& endSec) const;
    // 設定（グリッドの分割、BPM、MIDI の目標など）が変わったときに再描画させる
    void refreshView();
    // 選択中のノートを 1 つ返す（なければ 0）
    model::NoteId getPrimarySelection() const noexcept { return selection.empty() ? 0 : *selection.begin(); }

    // 選択中のノートの編集パラメータを変更する（スライダー操作用）。
    // begin/end でドラッグ全体を 1 回の Undo にまとめる。
    void beginParameterEdit();
    void applyToSelection(const std::function<void(model::NoteEdit&)>& change);
    void endParameterEdit();

    // 複数ノートの編集を 1 回の Undo で適用する（一括操作のボタン用）。変更があれば true。
    bool applyEdits(const std::vector<std::pair<model::NoteId, model::NoteEdit>>& edits);

    // 編集のコピー・貼り付け（F-27）。コピーは選択中の最初のノートの編集。
    void copySelectedEdit();
    void pasteToSelection();
    bool hasCopiedEdit() const noexcept { return copiedEdit.has_value(); }

    // 手描きのピッチ曲線を全て消す（F-22）
    void clearDrawing();

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
        Pan,
        Guide,
        Playhead,
        RangeNew,
        RangeStart,
        RangeEnd,
        Draw,
        Erase
    };

    struct DragState
    {
        DragKind kind = DragKind::None;
        juce::Point<int> startPoint;
        model::NoteId primary = 0;
        std::vector<model::Note> startNotes; // ドラッグ開始時の選択ノート
        double startViewSec = 0.0;
        double startTopMidi = 0.0;
        double startGuideOffset = 0.0;
        double rangeFixedSec = 0.0; // 範囲をドラッグしているときの、動かさない側の端（または新規範囲の起点）
        juce::Rectangle<int> rubber;
        double lastDrawSec = 0.0;  // 手描き・消去で、直前に処理したマウス位置（出力時間軸の秒）
        double lastDrawMidi = 0.0;
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
    void drawTempoGrid(juce::Graphics& g) const;
    void drawTargets(juce::Graphics& g) const;
    void drawGuide(juce::Graphics& g) const;
    void drawRange(juce::Graphics& g) const;
    void seekTo(double sec, bool force);
    double displayedPlayheadSec() const noexcept { return playheadSec >= 0.0 ? playheadSec : cursorSec; }
    bool hitTestPlayhead(juce::Point<float> p) const;
    void setTimingRange(double a, double b);
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
    int hitTestTarget(juce::Point<float> p) const;
    juce::Rectangle<float> targetBounds(const model::TargetNote& t) const;
    void clampView();
    void applyDrag(const juce::MouseEvent& e);
    // 出力時間軸の 2 点の間を手描き・消去する（F-22）
    void drawPitchBetween(double secA, double midiA, double secB, double midiB);
    void erasePitchBetween(double secA, double secB);
    // 出力時刻に対応する PitchTrack のフレーム（範囲外は端へ丸める）
    std::size_t frameOfOutput(double outSec) const;
    double drawMidiAt(const juce::MouseEvent& e) const;

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
    double cursorSec = -1.0; // 再生していないときに表示する位置（最後の再生位置、またはシークした位置）
    std::uint32_t lastSeekAtMs = 0;
    double hoverSec = 0.0;
    bool pendingNotify = false;
    bool editGestureOpen = false;

    // 描画用キャッシュ（revision が変わったときに更新）
    std::uint64_t cachedRevision = 0;
    bool cacheValid = false;
    render::TimeMap timeMap;
    model::TempoMap tempo;
    double timeOffset = 0.0;
    std::int64_t tempoRefreshedAt = 0;
    struct CurvePoint
    {
        double outSec;
        double midi;
        bool breakBefore;
        bool drawn; // 手描きの区間（F-22）
    };
    std::vector<CurvePoint> curve;

    std::optional<model::NoteEdit> copiedEdit; // 編集のコピー（F-27）

    std::shared_ptr<const plugin::SourceAudioData> peaksSource;
    std::vector<float> peakMin, peakMax;
    static constexpr int kPeakBlock = 256;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollView)
};
} // namespace koenote::ui
