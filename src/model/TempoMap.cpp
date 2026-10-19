#include "model/TempoMap.h"

#include <algorithm>
#include <cmath>

namespace koenote::model
{
TempoMap::TempoMap(std::vector<Entry> entries)
{
    for (const auto& e : entries)
    {
        if (! std::isfinite(e.sec) || ! std::isfinite(e.quarter))
            continue;

        if (! table.empty() && (e.sec <= table.back().sec || e.quarter <= table.back().quarter))
            continue;

        table.push_back(e);
    }

    if (table.size() < 2)
        table.clear();

    rebuildSegments();
}

//==============================================================================
// 拍子
namespace
{
constexpr double kEps = 1.0e-6;

bool isPowerOfTwo(int v) noexcept { return v > 0 && (v & (v - 1)) == 0; }
} // namespace

void TempoMap::setBarSignatures(std::vector<BarSignature> input)
{
    signatures.clear();

    for (const auto& s : input)
    {
        if (! std::isfinite(s.quarter) || s.numerator < 1 || s.numerator > 64 || ! isPowerOfTwo(s.denominator) || s.denominator > 64)
            continue;

        if (! signatures.empty() && s.quarter <= signatures.back().quarter + kEps)
            continue;

        signatures.push_back(s);
    }

    rebuildSegments();
}

void TempoMap::rebuildSegments()
{
    segments.clear();

    if (signatures.empty())
    {
        segments.push_back({});
        return;
    }

    long long bar = 1;

    for (std::size_t i = 0; i < signatures.size(); ++i)
    {
        const auto& s = signatures[i];
        Segment seg;
        seg.start = s.quarter;
        seg.beatLength = 4.0 / static_cast<double>(s.denominator);
        seg.beatsPerBar = s.numerator;
        seg.barLength = seg.beatLength * static_cast<double>(s.numerator);

        if (! segments.empty())
        {
            // 前の拍子の小節数（途中で拍子が変わった場合、途中までの小節も 1 小節と数える）
            const auto& prev = segments.back();
            bar += static_cast<long long>(std::ceil((seg.start - prev.start) / prev.barLength - kEps));
        }

        seg.firstBar = bar;
        segments.push_back(seg);
    }
}

std::size_t TempoMap::segmentIndexAt(double quarter) const noexcept
{
    std::size_t i = 0;

    while (i + 1 < segments.size() && quarter >= segments[i + 1].start - kEps)
        ++i;

    return i;
}

TempoMap::MeterPosition TempoMap::meterAt(double quarter) const noexcept
{
    MeterPosition m;

    if (segments.empty() || ! std::isfinite(quarter))
        return m;

    const auto& seg = segments[segmentIndexAt(quarter)];
    const double rel = quarter - seg.start;
    const double barIndex = std::floor(rel / seg.barLength + kEps);
    const double inBar = std::max(0.0, rel - barIndex * seg.barLength);
    const double beatIndex = std::floor(inBar / seg.beatLength + kEps);

    m.bar = seg.firstBar + static_cast<long long>(barIndex);
    m.beat = std::clamp(static_cast<int>(beatIndex) + 1, 1, seg.beatsPerBar);
    m.onBeat = std::abs(inBar - beatIndex * seg.beatLength) < kEps;
    m.onBar = m.onBeat && m.beat == 1;
    return m;
}

double TempoMap::beatLengthAt(double quarter) const noexcept
{
    return segments.empty() ? 1.0 : segments[segmentIndexAt(quarter)].beatLength;
}

double TempoMap::barLengthAt(double quarter) const noexcept
{
    return segments.empty() ? 4.0 : segments[segmentIndexAt(quarter)].barLength;
}

std::vector<TempoMap::GridPosition> TempoMap::positionsBetween(double q0, double q1, std::size_t maxCount, bool barsOnly) const
{
    std::vector<GridPosition> result;

    if (segments.empty() || ! std::isfinite(q0) || ! std::isfinite(q1) || q1 < q0)
        return result;

    for (std::size_t i = segmentIndexAt(q0); i < segments.size() && result.size() < maxCount; ++i)
    {
        const auto& seg = segments[i];
        // 区間の終わり（次の拍子の位置）。最初の区間はそれより前にも適用される。
        const double segEnd = i + 1 < segments.size() ? segments[i + 1].start : q1 + 1.0;
        const double from = i == 0 ? q0 : std::max(q0, seg.start);

        if (seg.start > q1 + kEps && i > 0)
            break;

        const double step = barsOnly ? seg.barLength : seg.beatLength;
        auto k = static_cast<long long>(std::ceil((from - seg.start) / step - kEps));

        for (;; ++k)
        {
            const double q = seg.start + static_cast<double>(k) * step;

            // 区間の終わり（次の拍子の位置）は含めない。範囲の終わり q1 は含める。
            if (q > q1 + kEps || q >= segEnd - kEps || result.size() >= maxCount)
                break;

            GridPosition p;
            p.quarter = q;

            if (barsOnly)
            {
                p.bar = seg.firstBar + k;
                p.beat = 1;
            }
            else
            {
                const auto barIndex = static_cast<long long>(std::floor(static_cast<double>(k) / seg.beatsPerBar));
                p.bar = seg.firstBar + barIndex;
                p.beat = static_cast<int>(k - barIndex * seg.beatsPerBar) + 1;
            }

            result.push_back(p);
        }
    }

    return result;
}

std::vector<TempoMap::GridPosition> TempoMap::barsBetween(double q0, double q1, std::size_t maxCount) const
{
    return positionsBetween(q0, q1, maxCount, true);
}

std::vector<TempoMap::GridPosition> TempoMap::beatsBetween(double q0, double q1, std::size_t maxCount) const
{
    return positionsBetween(q0, q1, maxCount, false);
}

TempoMap TempoMap::constant(double bpm)
{
    if (! (bpm > 0.0) || ! std::isfinite(bpm))
        return {};

    const double quartersPerSec = bpm / 60.0;
    return TempoMap({ { 0.0, 0.0 }, { 3600.0, 3600.0 * quartersPerSec } });
}

double TempoMap::secToQuarter(double sec) const noexcept
{
    if (! valid())
        return 0.0;

    std::size_t i = 1;

    if (sec >= table.back().sec)
        i = table.size() - 1;
    else if (sec > table.front().sec)
        i = static_cast<std::size_t>(std::upper_bound(table.begin(), table.end(), sec, [] (double v, const Entry& e) { return v < e.sec; })
                                     - table.begin());

    const auto& a = table[i - 1];
    const auto& b = table[i];
    return a.quarter + (sec - a.sec) * (b.quarter - a.quarter) / (b.sec - a.sec);
}

double TempoMap::quarterToSec(double quarter) const noexcept
{
    if (! valid())
        return 0.0;

    std::size_t i = 1;

    if (quarter >= table.back().quarter)
        i = table.size() - 1;
    else if (quarter > table.front().quarter)
        i = static_cast<std::size_t>(std::upper_bound(table.begin(), table.end(), quarter, [] (double v, const Entry& e) { return v < e.quarter; })
                                     - table.begin());

    const auto& a = table[i - 1];
    const auto& b = table[i];
    return a.sec + (quarter - a.quarter) * (b.sec - a.sec) / (b.quarter - a.quarter);
}

double TempoMap::bpmAt(double sec) const noexcept
{
    if (! valid())
        return 0.0;

    const double dq = secToQuarter(sec + 0.001) - secToQuarter(sec - 0.001);
    return dq / 0.002 * 60.0;
}

double snapToGrid(double sec, const TempoMap& tempo, double offsetSec, double gridQuarters) noexcept
{
    if (! tempo.valid() || ! (gridQuarters > 0.0))
        return sec;

    const double q = tempo.secToQuarter(sec + offsetSec);
    const double snapped = std::round(q / gridQuarters) * gridQuarters;
    return tempo.quarterToSec(snapped) - offsetSec;
}
} // namespace koenote::model
