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
