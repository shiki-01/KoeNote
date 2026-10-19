#include "render/GainEnvelope.h"

#include <algorithm>
#include <cmath>

namespace koenote::render
{
namespace
{
float dbToLinear(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
} // namespace

GainEnvelope::GainEnvelope(const std::vector<model::Note>& notes, double outDurationSec)
{
    const auto isNeutral = [] (const model::Note& n)
    {
        return n.edit.gainDb == 0.0f && n.edit.consonantGainDb == 0.0f && n.edit.breathGainDb == 0.0f;
    };

    if (outDurationSec <= 0.0 || std::all_of(notes.begin(), notes.end(), isNeutral))
        return;

    const auto count = static_cast<std::size_t>(std::ceil(outDurationSec / kResolutionSec)) + 1;
    std::vector<float> steps(count, 1.0f);

    const auto fill = [&] (double from, double to, float gain)
    {
        if (gain == 1.0f || to <= from)
            return;

        const auto i0 = static_cast<std::size_t>(std::max(0.0, std::floor(from / kResolutionSec)));
        const auto i1 = std::min(count, static_cast<std::size_t>(std::max(0.0, std::ceil(to / kResolutionSec))));

        for (std::size_t i = i0; i < i1; ++i)
            steps[i] = gain;
    };

    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        const double prevEnd = i > 0 ? notes[i - 1].outEndSec() : 0.0;
        const double consonantLen = std::min(kConsonantSec, std::max(0.0, n.outStartSec() - prevEnd));
        const double consonantStart = n.outStartSec() - consonantLen;

        // 子音（このノートの直前）
        fill(consonantStart, n.outStartSec(), dbToLinear(n.edit.consonantGainDb));

        // ノート本体
        fill(n.outStartSec(), n.outEndSec(), dbToLinear(n.edit.gainDb));

        // ブレス（このノートの直後 ～ 次のノートの子音の手前）
        double breathEnd = outDurationSec;
        if (i + 1 < notes.size())
        {
            const auto& next = notes[i + 1];
            const double nextConsonant = std::min(kConsonantSec, std::max(0.0, next.outStartSec() - n.outEndSec()));
            breathEnd = next.outStartSec() - nextConsonant;
        }

        fill(n.outEndSec(), breathEnd, dbToLinear(n.edit.breathGainDb));
    }

    // 約 10 ms（5 点）の移動平均で滑らかにする
    values.assign(count, 1.0f);
    constexpr int kHalf = 2;

    for (std::size_t i = 0; i < count; ++i)
    {
        double sum = 0.0;
        int num = 0;

        for (int k = -kHalf; k <= kHalf; ++k)
        {
            const auto j = static_cast<std::ptrdiff_t>(i) + k;
            if (j >= 0 && j < static_cast<std::ptrdiff_t>(count))
            {
                sum += steps[static_cast<std::size_t>(j)];
                ++num;
            }
        }

        values[i] = static_cast<float>(sum / num);
    }

    unity = false;
}

float GainEnvelope::gainAt(double outSec) const noexcept
{
    if (unity || values.empty() || outSec < 0.0)
        return 1.0f;

    const auto i = static_cast<std::size_t>(outSec / kResolutionSec);
    return i < values.size() ? values[i] : 1.0f;
}
} // namespace koenote::render
