#include "render/GainEnvelope.h"

#include "render/Sibilance.h"

#include <algorithm>
#include <cmath>

namespace koenote::render
{
namespace
{
float dbToLinear(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
} // namespace

GainEnvelope::GainEnvelope(const std::vector<model::Note>& notes, double outDurationSec, const Sibilance& sibilance)
{
    const bool useSibilance = sibilance.amount != nullptr && sibilance.track != nullptr && sibilance.timeMap != nullptr
                              && sibilance.amount->size() == sibilance.track->size() && hasSibilanceEdits(notes);

    const auto isNeutral = [] (const model::Note& n)
    {
        return n.edit.gainDb == 0.0f && n.edit.consonantGainDb == 0.0f && n.edit.breathGainDb == 0.0f;
    };

    if (outDurationSec <= 0.0 || (std::all_of(notes.begin(), notes.end(), isNeutral) && ! useSibilance))
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

    // 歯擦音（F-24）。出力時刻を元音声の時刻へ戻して、検出結果と属するノートを引く。
    if (useSibilance)
    {
        const auto& amount = *sibilance.amount;
        const auto& track = *sibilance.track;

        for (std::size_t i = 0; i < count; ++i)
        {
            const double src = sibilance.timeMap->toSource(static_cast<double>(i) * kResolutionSec);
            const auto frame = std::llround((src - track.startSec) / track.hopSec);

            if (frame < 0 || static_cast<std::size_t>(frame) >= amount.size())
                continue;

            const float a = amount[static_cast<std::size_t>(frame)];
            if (a <= 0.0f)
                continue;

            const int owner = sibilanceOwner(notes, src);
            if (owner < 0)
                continue;

            const float gain = dbToLinear(notes[static_cast<std::size_t>(owner)].edit.sibilanceGainDb);
            steps[i] *= 1.0f + a * (gain - 1.0f);
        }
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
