#pragma once

#include "model/Note.h"

#include <vector>

namespace koenote::render
{
// ノートごとのゲイン編集（F-10）を、出力時間軸上のゲイン曲線（線形）にしたもの。
//  - ノート区間: gainDb
//  - ノート直前の子音部（最大 kConsonantSec、直前のノートとの隙間の範囲内）: 次のノートの consonantGainDb
//  - ノート直後から次のノートの子音部の手前まで（最後のノートは音声の終わりまで）: breathGainDb
// 区間の境界は約 10 ms の移動平均で滑らかにつなぐ（クリックノイズを避ける）。
class GainEnvelope
{
public:
    static constexpr double kConsonantSec = 0.08;
    static constexpr double kResolutionSec = 0.002;

    GainEnvelope() = default;
    GainEnvelope(const std::vector<model::Note>& notes, double outDurationSec);

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
