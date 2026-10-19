#pragma once

#include "model/Note.h"
#include "model/PitchDrawing.h"
#include "model/PitchTrack.h"

#include <vector>

namespace koenote::render
{
// ノートごとの編集パラメータから、PitchTrack の各フレームに加えるピッチ補正量（セント）を求める。
// 戻り値の長さは track.size()。ノート外のフレームは 0。
//
// 元のピッチ曲線のノート内偏差 dev（中心 detectedMidi からのセント）を、
//   drift   : 約 0.3 秒の移動平均（ゆるやかな変動）
//   vibrato : 約 40 ms の移動平均 − drift（ビブラート成分）
//   residual: dev − 約 40 ms の移動平均（細かい揺れ）
// に分解し、
//   新しい偏差 = driftScale * (1 - centerCorrection) * drift + vibratoScale * vibrato + residual
// とする。補正量 = pitchOffsetCents + (新しい偏差 - dev)。
// 既定値（無編集）では補正量は 0 になる。隣接するノートの境界は transitionSmoothness に応じて滑らかにつなぐ。
// centerCorrection は、ノートの頭の attackHoldSec・尻の releaseHoldSec では 0 から滑らかに効かせる（F-23）。
// drawing があれば、描いた区間の有声フレームは描いた音高になるように置き換える（F-22。端は 20 ms でつなぐ）。
std::vector<float> computePitchAdjustCents(const model::PitchTrack& track,
                                          const std::vector<model::Note>& notes,
                                          const model::PitchDrawing* drawing = nullptr);

// フレームごとのフォルマントのシフト量（セント。F-09）。ノート内では
//   formantShiftCents + formantFollow * pitchAdjustCents[frame]
// ノート外は 0。pitchAdjust は computePitchAdjustCents の結果（同じ長さ）。
std::vector<float> computeFormantShiftCents(const model::PitchTrack& track,
                                            const std::vector<model::Note>& notes,
                                            const std::vector<float>& pitchAdjustCents);
} // namespace koenote::render
