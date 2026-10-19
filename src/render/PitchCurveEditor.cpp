#include "render/PitchCurveEditor.h"

#include <algorithm>
#include <cmath>

namespace koenote::render
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

double toMidi(float hz) noexcept
{
    return 69.0 + 12.0 * std::log2(static_cast<double>(hz) / 440.0);
}

// 有声フレームだけを対象にした、端を正規化する移動平均（窓は ±halfFrames）
std::vector<double> movingAverage(const std::vector<double>& value, const std::vector<char>& valid, std::size_t halfFrames)
{
    const std::size_t n = value.size();
    std::vector<double> prefixSum(n + 1, 0.0);
    std::vector<std::size_t> prefixCount(n + 1, 0);

    for (std::size_t i = 0; i < n; ++i)
    {
        prefixSum[i + 1] = prefixSum[i] + (valid[i] ? value[i] : 0.0);
        prefixCount[i + 1] = prefixCount[i] + (valid[i] ? 1u : 0u);
    }

    std::vector<double> out(n, 0.0);

    for (std::size_t i = 0; i < n; ++i)
    {
        const std::size_t lo = i >= halfFrames ? i - halfFrames : 0;
        const std::size_t hi = std::min(n, i + halfFrames + 1);
        const auto count = prefixCount[hi] - prefixCount[lo];
        out[i] = count > 0 ? (prefixSum[hi] - prefixSum[lo]) / static_cast<double>(count) : 0.0;
    }

    return out;
}
} // namespace

std::vector<float> computePitchAdjustCents(const model::PitchTrack& track, const std::vector<model::Note>& notes)
{
    std::vector<float> adjust(track.size(), 0.0f);

    if (track.empty())
        return adjust;

    const double hop = track.hopSec;
    const auto frameOf = [&] (double sec)
    {
        return static_cast<long long>(std::ceil((sec - track.startSec) / hop - 1.0e-9));
    };

    for (const auto& note : notes)
    {
        const auto first = std::max<long long>(0, frameOf(note.startSec));
        const auto last = std::min<long long>(static_cast<long long>(track.size()) - 1, frameOf(note.endSec) - 1);

        if (last < first)
            continue;

        const auto count = static_cast<std::size_t>(last - first + 1);
        std::vector<double> dev(count, 0.0);
        std::vector<char> voiced(count, 0);

        for (std::size_t i = 0; i < count; ++i)
        {
            const auto f = static_cast<std::size_t>(first) + i;

            if (track.isVoiced(f))
            {
                dev[i] = (toMidi(track.f0Hz[f]) - static_cast<double>(note.detectedMidi)) * 100.0;
                voiced[i] = 1;
            }
        }

        const auto driftHalf = static_cast<std::size_t>(std::max(1.0, std::round(0.15 / hop)));
        const auto fineHalf = static_cast<std::size_t>(std::max(1.0, std::round(0.02 / hop)));

        const auto drift = movingAverage(dev, voiced, driftHalf);
        const auto fine = movingAverage(dev, voiced, fineHalf);

        const double driftGain = static_cast<double>(note.edit.driftScale) * (1.0 - static_cast<double>(note.edit.centerCorrection));
        const double vibratoGain = static_cast<double>(note.edit.vibratoScale);

        for (std::size_t i = 0; i < count; ++i)
        {
            double change = 0.0;

            if (voiced[i])
            {
                const double vibrato = fine[i] - drift[i];
                const double residual = dev[i] - fine[i];
                const double newDev = driftGain * drift[i] + vibratoGain * vibrato + residual;
                change = newDev - dev[i];
            }

            adjust[static_cast<std::size_t>(first) + i] = static_cast<float>(static_cast<double>(note.edit.pitchOffsetCents) + change);
        }
    }

    // 隣り合うノートの境界を滑らかにつなぐ
    for (std::size_t i = 0; i + 1 < notes.size(); ++i)
    {
        const auto& a = notes[i];
        const auto& b = notes[i + 1];

        if (b.startSec - a.endSec > 0.08)
            continue;

        const double smooth = static_cast<double>(std::max(a.edit.transitionSmoothness, b.edit.transitionSmoothness));
        const double width = 0.01 + 0.19 * std::clamp(smooth, 0.0, 1.0);
        const double boundary = 0.5 * (a.endSec + b.startSec);

        const auto f0 = std::max<long long>(0, frameOf(boundary - 0.5 * width));
        const auto f1 = std::min<long long>(static_cast<long long>(track.size()) - 1, frameOf(boundary + 0.5 * width));

        if (f1 - f0 < 2)
            continue;

        const float left = adjust[static_cast<std::size_t>(f0)];
        const float right = adjust[static_cast<std::size_t>(f1)];

        for (long long f = f0; f <= f1; ++f)
        {
            const double u = static_cast<double>(f - f0) / static_cast<double>(f1 - f0);
            const double s = 0.5 - 0.5 * std::cos(kPi * u);
            adjust[static_cast<std::size_t>(f)] = static_cast<float>(left + (right - left) * s);
        }
    }

    return adjust;
}
} // namespace koenote::render
