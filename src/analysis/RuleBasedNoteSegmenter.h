#pragma once

#include "analysis/PitchAnalyzer.h"

namespace koenote::analysis
{
// F0 系列を規則に従ってノートに分割する。
// 手順: 無声の橋渡し → 有声区間ごとの移動 median → ヒステリシス付きの分割 → 短ノートの併合。
class RuleBasedNoteSegmenter final : public INoteSegmenter
{
public:
    std::vector<model::Note> segment(const model::PitchTrack& track, const MonoAudioView& audio) override;
};
} // namespace koenote::analysis
