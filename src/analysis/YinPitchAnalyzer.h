#pragma once

#include "analysis/PitchAnalyzer.h"

namespace koenote::analysis
{
// YIN（差分関数・累積平均正規化）に基づく pYIN 風の F0 推定。
// 各フレームの候補を Viterbi で時系列に平滑化し、有声・無声を決める。
// 内部状態を持たないため、同一インスタンスを複数スレッドから同時に呼んでもよい。
class YinPitchAnalyzer final : public IPitchAnalyzer
{
public:
    struct Params
    {
        float fMinHz = 65.0f;
        float fMaxHz = 1100.0f;
        double hopSec = 0.005;
        float voicedThreshold = 0.25f; // 選ばれた候補の d' がこれを超えるフレームは無声
        float silenceDb = -55.0f;      // フレーム RMS がこれ未満（dBFS）なら無声固定
    };

    YinPitchAnalyzer();
    explicit YinPitchAnalyzer(const Params& params);

    model::PitchTrack analyze(const MonoAudioView& audio, const ProgressCallback& progress) override;

private:
    Params params;
};
} // namespace koenote::analysis
