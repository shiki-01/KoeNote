#pragma once

#include "model/ModelVersion.h"
#include "model/Note.h"
#include "model/PitchDrawing.h"
#include "model/PitchTrack.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace koenote::model
{
// 1 つの ARA AudioModification に対応する編集モデル（要件 5章「編集モデル層」）。
// message thread からのみ操作する。オーディオスレッドからは触らない
// （レンダリングには値コピーした snapshot を渡す）。
class EditModel
{
public:
    static constexpr double kMinNoteDurationSec = 0.03;
    static constexpr std::size_t kMaxUndoDepth = 256;
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    //==========================================================================
    // 解析結果
    bool hasAnalysis() const noexcept { return analysed; }
    const PitchTrack& pitchTrack() const noexcept { return track; }
    const std::vector<Note>& notes() const noexcept { return noteList; }
    const Note* findNote(NoteId id) const noexcept;
    // id の添字。見つからなければ npos
    std::size_t indexOf(NoteId id) const noexcept;
    // 時刻（元音声の秒）を含むノートの id。なければ 0
    NoteId noteAt(double sourceSec) const noexcept;

    // 解析結果を設定する。ノートに id を採番し、Undo 履歴を破棄する。
    void setAnalysis(PitchTrack newTrack, std::vector<Note> newNotes);

    //==========================================================================
    // 編集操作。いずれも不正な結果になる場合は何も変更せず false を返す。
    // ドラッグ操作などで複数回の変更を 1 つの Undo にまとめるときは beginEdit/endEdit で囲む。
    void beginEdit();
    void endEdit();

    bool setNotePitchOffset(NoteId id, float cents);
    bool setNoteEdit(NoteId id, const NoteEdit& edit);
    // 編集後のノート区間（出力時間軸）を設定する
    bool setNoteTiming(NoteId id, double targetStartSec, double targetEndSec);
    // 元音声の時刻 atSourceSec でノートを 2 つに分割する
    bool splitNote(NoteId id, double atSourceSec, NoteId* rightId = nullptr);
    // id のノートと、直後のノートを結合する
    bool mergeWithNext(NoteId id);
    // id のノートと直後のノートの境界（元音声の時刻）を移動する
    bool moveBoundary(NoteId id, double newBoundarySec);
    // ノートの編集内容を初期状態に戻す
    bool resetNote(NoteId id);
    // 全ノートの編集内容と手描きのピッチ曲線を初期状態に戻す
    bool resetAll();

    //==========================================================================
    // 手描きのピッチ曲線（F-22。Undo の対象）。フレームは PitchTrack のフレーム。
    const PitchDrawing& drawing() const noexcept { return pitchDrawing; }
    // firstFrame から midi（MIDI ノート番号）を描く。PitchTrack の範囲を超える分は切り捨てる。
    bool drawPitch(std::size_t firstFrame, const std::vector<float>& midi);
    // [firstFrame, endFrame) の手描きを消す
    bool erasePitch(std::size_t firstFrame, std::size_t endFrame);
    bool clearDrawing();

    //==========================================================================
    // Undo / Redo
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    bool undo();
    bool redo();

    // 内容が変わるたびに増える。レンダリングキャッシュの更新判定に使う。
    std::uint64_t revision() const noexcept { return revisionCounter; }

    //==========================================================================
    // 複数ノートの一括編集（F-16）。1 回の Undo で戻る。タイミングは、隣のノートと重ならない・
    // 最短長を満たすように前から順に調整される（後ろのノートが押し出されることがある）。
    bool applyEdits(const std::vector<std::pair<NoteId, NoteEdit>>& edits);

    //==========================================================================
    // MIDI の目標ノート（F-12）。assignments は (ノート id, 目標の添字)。
    const std::vector<TargetNote>& targets() const noexcept { return targetList; }
    // 割り当てられた目標の添字。未割り当てなら -1。
    int assignmentOf(NoteId id) const noexcept;
    bool setTargets(std::vector<TargetNote> newTargets, std::vector<std::pair<NoteId, int>> newAssignments);
    // 目標への割り当てを 1 件変更する（targetIndex = -1 で解除）。同じ目標が他のノートに割り当てられていれば、そちらは解除する。
    bool assignTarget(NoteId id, int targetIndex);
    bool clearTargets();

    //==========================================================================
    // 設定（Undo の対象外。保存される）。変更すると revision が増える。
    const ModelSettings& settings() const noexcept { return modelSettings; }
    void setSettings(const ModelSettings& s);

    //==========================================================================
    // 保存・復元（バージョン付きバイナリ形式。kEditModelFormatVersion を参照）
    std::vector<std::uint8_t> serialize() const;
    // 不正なデータ、または未対応の新しいバージョンの場合は nullopt
    static std::optional<EditModel> deserialize(const std::uint8_t* data, std::size_t size);

private:
    struct Snapshot
    {
        std::vector<Note> notes;
        NoteId nextId = 1;
        std::vector<TargetNote> targets;
        std::vector<std::pair<NoteId, int>> assignments;
        PitchDrawing drawing;
    };

    Snapshot makeSnapshot() const { return { noteList, nextNoteId, targetList, assignmentList, pitchDrawing }; }
    void restore(const Snapshot& s);
    static bool isValid(const std::vector<Note>& notes) noexcept;

    // 変更を試行する。mutator が false を返す、または結果が不正なら元に戻す。
    template <typename Fn>
    bool mutate(Fn&& mutator);
    void commit(Snapshot before);

    bool analysed = false;
    PitchTrack track;
    std::vector<Note> noteList;
    NoteId nextNoteId = 1;
    std::vector<TargetNote> targetList;
    std::vector<std::pair<NoteId, int>> assignmentList;
    PitchDrawing pitchDrawing;
    ModelSettings modelSettings;
    std::uint64_t revisionCounter = 0;

    std::vector<Snapshot> undoStack;
    std::vector<Snapshot> redoStack;

    int editDepth = 0;
    std::optional<Snapshot> pendingBefore; // beginEdit 時点の状態
    bool pendingChanged = false;
};
} // namespace koenote::model
