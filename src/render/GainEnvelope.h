#pragma once

#include "model/Note.h"
#include "model/PitchTrack.h"
#include "render/TimeMap.h"

#include <vector>

namespace koenote::render
{
// ノートごとのゲイン編集（F-10）を、出力時間軸上のゲイン曲線（線形）にしたもの。
//  - ノート区間: gainDb
//  - ノート直前の子音部（最大 kConsonantSec、直前のノートとの隙間の範囲内）: 次のノートの consonantGainDb
//  - ノート直後から次のノートの子音部の手前まで（最後のノートは音声の終わりまで）: breathGainDb
//  - 歯擦音（F-24。sibilance を渡した場合）: 歯擦音らしさ a（0..1）に応じて、属するノートの sibilanceGainDb を
//    1 + a × (ゲイン − 1) の倍率で上の値に掛ける
// 区間の境界は約 10 ms の移動平均で滑らかにつなぐ（クリックノイズを避ける）。
class GainEnvelope
{
public:
    static constexpr double kConsonantSec = 0.08;
    static constexpr double kResolutionSec = 0.002;

    // 歯擦音の検出結果（detectSibilance）と、出力時間 → 元音声の時間の対応。いずれかが nullptr なら歯擦音は扱わない。
    struct Sibilance
    {
        const std::vector<float>* amount = nullptr; // PitchTrack のフレームごと
        const model::PitchTrack* track = nullptr;
        const TimeMap* timeMap = nullptr;
    };

    GainEnvelope() = default;
    GainEnvelope(const std::vector<model::Note>& notes, double outDurationSec, const Sibilance& sibilance = {});

    // ゲインが全区間で 1（変更なし）か
    bool isUnity() const noexcept { return unity; }

    // 出力時刻（秒）のゲイン（線形）。範囲外は 1。
    float gainAt(double outSec) const noexcept;

    const std::vector<float>& samples() const noexcept { return values; }

private:
    std::vector<float> values;
    bool unity = true;
};
} // namespace koenote::render
