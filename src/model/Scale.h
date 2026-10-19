#pragma once

#include <cmath>

namespace koenote::model
{
// ピッチのスナップ（F-04）。UI のドラッグ移動と、一括補正（F-16）で共通に使う。
enum class SnapMode
{
    Off,
    Semitone,
    Scale
};

enum class ScaleType
{
    Major,
    Minor
};

// pitchClass（0 = C .. 11 = B）が、root を主音とするスケールの構成音か
inline bool isInScale(int pitchClass, int root, ScaleType scale) noexcept
{
    constexpr bool major[12] = { true, false, true, false, true, true, false, true, false, true, false, true };
    constexpr bool minor[12] = { true, false, true, true, false, true, false, true, true, false, true, false };
    const int rel = (((pitchClass - root) % 12) + 12) % 12;
    return scale == ScaleType::Major ? major[rel] : minor[rel];
}

// midi を、指定のスナップ設定で最も近い音へ丸める
inline float snapMidi(float midi, SnapMode mode, int root, ScaleType scale) noexcept
{
    if (mode == SnapMode::Off)
        return midi;

    if (mode == SnapMode::Semitone)
        return std::round(midi);

    const int centre = static_cast<int>(std::round(midi));
    float best = static_cast<float>(centre);
    float bestDistance = 1.0e9f;

    for (int candidate = centre - 6; candidate <= centre + 6; ++candidate)
    {
        if (! isInScale((((candidate % 12) + 12) % 12), root, scale))
            continue;

        const float distance = std::abs(static_cast<float>(candidate) - midi);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = static_cast<float>(candidate);
        }
    }

    return best;
}
} // namespace koenote::model
