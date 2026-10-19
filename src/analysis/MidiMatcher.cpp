#include "analysis/MidiMatcher.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>

namespace koenote::analysis
{
namespace
{
// 動的計画法の遷移。Pair: 両側を対応付け、SkipAnalysed: 解析側のみスキップ、SkipTarget: ターゲット側のみスキップ
enum class Step : std::uint8_t
{
    Pair,
    SkipAnalysed,
    SkipTarget,
};

// startSec 昇順の添字列（安定ソートで同時刻の順序を保つ）
std::vector<std::size_t> sortedOrder(const std::vector<TimedNote>& notes)
{
    std::vector<std::size_t> order(notes.size());
    std::iota(order.begin(), order.end(), std::size_t{ 0 });
    std::stable_sort(order.begin(), order.end(), [&notes](std::size_t a, std::size_t b) {
        return notes[a].startSec < notes[b].startSec;
    });
    return order;
}

// ペアのコスト。許容範囲を超える場合は無限大（対応付け禁止）
double pairCost(const TimedNote& a, const TimedNote& t, const MatchOptions& options)
{
    const double onsetDiff = std::abs(a.startSec - t.startSec);
    const double pitchDiff = std::abs(static_cast<double>(a.midi) - static_cast<double>(t.midi));
    if (onsetDiff > options.maxOnsetDiffSec || pitchDiff > options.maxPitchDiffSemitones)
        return std::numeric_limits<double>::infinity();

    const double endDiff = std::abs(a.endSec - t.endSec);
    return options.timeWeight * (onsetDiff + endDiff) / 2.0 + options.pitchWeight * pitchDiff;
}
} // namespace

MatchResult matchNotes(const std::vector<TimedNote>& analysed,
                       const std::vector<TimedNote>& target,
                       const MatchOptions& options)
{
    MatchResult result;

    const auto orderA = sortedOrder(analysed);
    const auto orderT = sortedOrder(target);
    const std::size_t n = orderA.size();
    const std::size_t m = orderT.size();
    const std::size_t width = m + 1;

    // 費用は直前の行のみ保持し、遷移の記録は全セル分を持つ（1 バイト/セル）
    std::vector<double> prev(width, 0.0);
    std::vector<double> cur(width, 0.0);
    std::vector<Step> steps((n + 1) * width, Step::Pair);

    for (std::size_t j = 1; j <= m; ++j)
    {
        prev[j] = prev[j - 1] + options.skipCost;
        steps[j] = Step::SkipTarget;
    }

    for (std::size_t i = 1; i <= n; ++i)
    {
        cur[0] = prev[0] + options.skipCost;
        steps[i * width] = Step::SkipAnalysed;

        const TimedNote& a = analysed[orderA[i - 1]];
        for (std::size_t j = 1; j <= m; ++j)
        {
            const TimedNote& t = target[orderT[j - 1]];

            // 同点の場合は Pair > SkipAnalysed > SkipTarget の順で優先する
            double best = prev[j - 1] + pairCost(a, t, options);
            Step step = Step::Pair;

            const double up = prev[j] + options.skipCost;
            if (up < best)
            {
                best = up;
                step = Step::SkipAnalysed;
            }

            const double left = cur[j - 1] + options.skipCost;
            if (left < best)
            {
                best = left;
                step = Step::SkipTarget;
            }

            cur[j] = best;
            steps[i * width + j] = step;
        }
        std::swap(prev, cur);
    }

    // 後ろから遷移をたどって対応付けを復元する
    std::vector<bool> matchedA(analysed.size(), false);
    std::vector<bool> matchedT(target.size(), false);

    std::size_t i = n;
    std::size_t j = m;
    while (i > 0 || j > 0)
    {
        const Step step = steps[i * width + j];
        if (i > 0 && j > 0 && step == Step::Pair)
        {
            result.pairs.emplace_back(static_cast<int>(orderA[i - 1]), static_cast<int>(orderT[j - 1]));
            matchedA[orderA[i - 1]] = true;
            matchedT[orderT[j - 1]] = true;
            --i;
            --j;
        }
        else if (i > 0 && (j == 0 || step == Step::SkipAnalysed))
        {
            --i;
        }
        else
        {
            --j;
        }
    }
    std::reverse(result.pairs.begin(), result.pairs.end());

    for (std::size_t k = 0; k < analysed.size(); ++k)
    {
        if (! matchedA[k])
            result.unmatchedAnalysis.push_back(static_cast<int>(k));
    }
    for (std::size_t k = 0; k < target.size(); ++k)
    {
        if (! matchedT[k])
            result.unmatchedTarget.push_back(static_cast<int>(k));
    }

    return result;
}
} // namespace koenote::analysis
