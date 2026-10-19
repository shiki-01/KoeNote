#pragma once

#include <cstddef>
#include <vector>

namespace koenote::model
{
// フレーム単位の F0 推定結果。元音声の時間軸で、フレーム i の時刻は startSec + i * hopSec。
struct PitchTrack
{
    double startSec = 0.0;
    double hopSec = 0.005;
    std::vector<float> f0Hz;    // 無声フレームは 0
    std::vector<float> voicing; // 有声確率（0..1）。f0Hz と同じ長さ

    std::size_t size() const noexcept { return f0Hz.size(); }
    bool empty() const noexcept { return f0Hz.empty(); }
    double timeOf(std::size_t frame) const noexcept { return startSec + static_cast<double>(frame) * hopSec; }
    bool isVoiced(std::size_t frame) const noexcept { return frame < f0Hz.size() && f0Hz[frame] > 0.0f; }
};
} // namespace koenote::model
