#pragma once

#include "model/Note.h"
#include "model/Scale.h"

#include <utility>
#include <vector>

namespace koenote::model
{
// MIX 向けの一括操作（要件 3章「MIX向けの機能」）。いずれも ids の各ノート（空なら全ノート）に対する編集内容を返す。
// 戻り値は EditModel::applyEdits に渡す（1 回の Undo で戻る）。

// 音量の均し（F-25）。対象ノートの音量（Note::level）の dB 平均へ、strength（0..1）の割合だけ寄せるゲインを gainDb に設定する
// （以前の gainDb は置き換える）。変更量は ±maxDb に制限する。音量が 0 のノートは変更しない。
std::vector<std::pair<NoteId, NoteEdit>> computeLevelingEdits(const std::vector<Note>& notes,
                                                              const std::vector<NoteId>& ids,
                                                              float strength,
                                                              float maxDb = 12.0f);

// ゲインの一括設定（F-24・F-26）
enum class GainTarget
{
    Note,      // gainDb
    Consonant, // consonantGainDb
    Breath,    // breathGainDb（ノートの後ろのブレス・無声区間）
    Sibilance  // sibilanceGainDb
};

std::vector<std::pair<NoteId, NoteEdit>> computeGainEdits(const std::vector<Note>& notes,
                                                          const std::vector<NoteId>& ids,
                                                          GainTarget target,
                                                          float db);

// 編集の貼り付け（F-27）。src の「音の調整」の項目（ピッチ中心の補正、ビブラート、ドリフト、遷移、頭・尻の除外、
// フォルマント、ゲイン類）を dst へ写す。ピッチ移動量（pitchOffsetCents）とタイミングはノートごとに異なるため写さない。
NoteEdit pasteEditParameters(const NoteEdit& dst, const NoteEdit& src) noexcept;

// スナップ先からのずれ（セント。F-28）。編集後のピッチ中心（targetMidi）を mode でスナップした音との差。
// mode が Off のときは半音へのずれを返す。
float pitchDeviationCents(const Note& note, SnapMode mode, int root, ScaleType scale) noexcept;

// ガイドのピッチへの追従（F-29）
struct GuideFollowParams
{
    enum class Mode
    {
        Unison,      // ガイドと同じ音高へ（ダブル）
        KeepInterval // ガイドとの音程差（最も近い半音の数）を保ち、ガイドの実際の音高に合わせる（ハモリ）
    };

    Mode mode = Mode::Unison;
    float strength = 1.0f;   // 0..1
    double offsetSec = 0.0;  // ガイド上の時刻 = このクリップの（出力時間軸の）時刻 + offsetSec
    double minOverlap = 0.3; // ノートの長さに対する、ガイドのノートとの重なりの最小割合
};

// guideNotes（ガイドの元音声の時間軸。ガイドは時間の編集をしない）の中で、各ノートと最も長く重なるノートを対応付け、
// ピッチ移動量（pitchOffsetCents）を設定する。対応するノートがないノートは変更しない。matchedOut に対応付けた数を返す。
std::vector<std::pair<NoteId, NoteEdit>> computeGuidePitchEdits(const std::vector<Note>& notes,
                                                                const std::vector<NoteId>& ids,
                                                                const std::vector<Note>& guideNotes,
                                                                const GuideFollowParams& params,
                                                                int* matchedOut = nullptr);
} // namespace koenote::model
