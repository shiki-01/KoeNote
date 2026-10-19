#pragma once

#include "analysis/PitchAnalyzer.h"

namespace koenote::analysis
{
// 何も検出しない空の実装（フェーズ0 の stub、およびテスト用）
class NullPitchAnalyzer final : public IPitchAnalyzer, public INoteSegmenter
{
public:
    model::PitchTrack analyze(const MonoAudioView&, const ProgressCallback&) override { return {}; }
    std::vector<model::Note> segment(const model::PitchTrack&, const MonoAudioView&) override { return {}; }
};
} // namespace koenote::analysis
