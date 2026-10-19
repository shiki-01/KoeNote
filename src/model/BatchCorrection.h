#pragma once

#include "model/Note.h"
#include "model/Scale.h"
#include "model/TempoMap.h"

#include <utility>
#include <vector>

namespace koenote::model
{
// 一括補正（F-16）のパラメータ。
struct BatchParams
{
    // --- ピッチ ---
    float pitchStrength = 1.0f;     // 0..1。検出したピッチ中心を、スナップした音へ近づける割合（0 で変更なし）
    SnapMode snapMode = SnapMode::Semitone;
    int snapRoot = 0;
    ScaleType snapScale = ScaleType::Major;
    float vibratoKeep = 1.0f;       // 0..1。ビブラートを残す割合（1 = そのまま、0 = 除去）

    // --- タイミング ---
    float timingStrength = 0.0f;    // 0..1。ノートの開始位置をグリッドへ近づける割合（0 で変更なし）
    double gridQuarters = 0.25;     // グリッドの間隔（四分音符単位。0.25 = 16 分音符）
    bool keepDuration = true;       // true: 長さを保つ（開始位置だけ動かす）、false: 終了位置もグリッドへ寄せる
    TempoMap tempo;                 // 無効ならタイミングは変更しない
    double offsetSec = 0.0;         // 編集の時間軸 → プロジェクトの時刻のずれ（秒）
};

// ids の各ノート（空なら全ノート）に対する編集内容を計算する。ピッチの項目（pitchOffsetCents、centerCorrection、
// vibratoScale）は検出値から計算し直すため、以前のピッチ編集は置き換えられる。
// 戻り値は EditModel::applyEdits に渡す。
std::vector<std::pair<NoteId, NoteEdit>> computeBatchEdits(const std::vector<Note>& notes,
                                                           const std::vector<NoteId>& ids,
                                                           const BatchParams& params);
} // namespace koenote::model
