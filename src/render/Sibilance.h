#pragma once

#include "model/Note.h"
#include "model/PitchTrack.h"

#include <cstddef>
#include <vector>

namespace koenote::render
{
// 歯擦音（サ行などの無声の高域成分）の検出（F-24）。PitchTrack のフレームごとに、歯擦音らしさ（0..1）を返す
// （長さ track.size()）。無声のフレームのうち、4 kHz 以上の成分がエネルギーの大半を占め、ある程度の音量があるものを
// 歯擦音とみなす。サンプルレートが 12 kHz 未満なら全て 0。
std::vector<float> detectSibilance(const float* mono, std::size_t numFrames, double sampleRate, const model::PitchTrack& track);

// いずれかのノートに歯擦音のゲインが設定されているか（検出を省くための判定）
bool hasSibilanceEdits(const std::vector<model::Note>& notes) noexcept;

// 元音声の時刻 sourceSec の歯擦音が属するノートの添字（notes は元音声の時間で昇順）。
// 直後のノートの開始まで kLeadSec 以内なら直後のノート（語頭の子音）、そうでなければ直前のノート（語尾）。ノートがなければ -1。
int sibilanceOwner(const std::vector<model::Note>& notes, double sourceSec) noexcept;

inline constexpr double kSibilanceLeadSec = 0.3;
} // namespace koenote::render
