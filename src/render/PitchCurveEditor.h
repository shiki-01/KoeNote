#pragma once

#include "model/Note.h"
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
std::vector<float> computePitchAdjustCents(const model::PitchTrack& track, const std::vector<model::Note>& notes);
} // namespace koenote::render
