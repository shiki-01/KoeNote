#include "render/TimeMap.h"

#include <algorithm>

namespace koenote::render
{
namespace
{
constexpr double kEps = 1.0e-9;
} // namespace

TimeMap::TimeMap(std::vector<Anchor> anchorsIn) : anchors(std::move(anchorsIn))
{
    for (std::size_t i = 1; i < anchors.size(); ++i)
    {
        anchors[i].outSec = std::max(anchors[i].outSec, anchors[i - 1].outSec);
        anchors[i].srcSec = std::max(anchors[i].srcSec, anchors[i - 1].srcSec);
    }
}

TimeMap TimeMap::fromNotes(const std::vector<model::Note>& notes, double consonantSec)
{
    std::vector<Anchor> a;

    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];

        if (i == 0)
        {
            // 最初のノートより前は平行移動。出力時刻が負にならない範囲で始点を置く。
            const double back = std::min(n.startSec, n.outStartSec());
            a.push_back({ n.outStartSec() - back, n.startSec - back });
        }
        else
        {
            const auto& p = notes[i - 1];
            const double srcGap = n.startSec - p.endSec;
            const double outGap = n.outStartSec() - p.outEndSec();

            if (srcGap > kEps && outGap > kEps)
            {
                const double c = std::min({ consonantSec, 0.5 * srcGap, 0.5 * outGap });
                a.push_back({ n.outStartSec() - c, n.startSec - c });
            }
        }

        a.push_back({ n.outStartSec(), n.startSec });
        a.push_back({ n.outEndSec(), n.endSec });
    }

    return TimeMap(std::move(a));
}

double TimeMap::toSource(double outSec) const noexcept
{
    if (anchors.empty())
        return outSec;

    if (outSec <= anchors.front().outSec)
        return anchors.front().srcSec + (outSec - anchors.front().outSec);

    if (outSec >= anchors.back().outSec)
        return anchors.back().srcSec + (outSec - anchors.back().outSec);

    // outSec を含む区間 [i-1, i] を探す（outSec が一致するアンカーが複数ある場合は最後のものを使う）
    const auto it = std::upper_bound(anchors.begin(), anchors.end(), outSec,
                                     [] (double v, const Anchor& a) { return v < a.outSec; });
    const auto& hi = *it;
    const auto& lo = *(it - 1);
    const double span = hi.outSec - lo.outSec;

    if (span <= kEps)
        return hi.srcSec;

    return lo.srcSec + (hi.srcSec - lo.srcSec) * (outSec - lo.outSec) / span;
}

double TimeMap::slopeAt(double outSec) const noexcept
{
    if (anchors.empty() || outSec < anchors.front().outSec || outSec >= anchors.back().outSec)
        return 1.0;

    const auto it = std::upper_bound(anchors.begin(), anchors.end(), outSec,
                                     [] (double v, const Anchor& a) { return v < a.outSec; });
    const auto& hi = *it;
    const auto& lo = *(it - 1);
    const double span = hi.outSec - lo.outSec;

    return span <= kEps ? 1.0 : (hi.srcSec - lo.srcSec) / span;
}

double TimeMap::toOutput(double srcSec) const noexcept
{
    if (anchors.empty())
        return srcSec;

    if (srcSec <= anchors.front().srcSec)
        return anchors.front().outSec + (srcSec - anchors.front().srcSec);

    if (srcSec >= anchors.back().srcSec)
        return anchors.back().outSec + (srcSec - anchors.back().srcSec);

    // srcSec を含む区間を探す（srcSec が一致するアンカーが複数ある場合は最初のものを使う）
    const auto it = std::lower_bound(anchors.begin(), anchors.end(), srcSec,
                                     [] (const Anchor& a, double v) { return a.srcSec < v; });
    const auto& hi = *it;
    const auto& lo = *(it - 1);
    const double span = hi.srcSec - lo.srcSec;

    if (span <= kEps)
        return lo.outSec;

    return lo.outSec + (hi.outSec - lo.outSec) * (srcSec - lo.srcSec) / span;
}
} // namespace koenote::render
