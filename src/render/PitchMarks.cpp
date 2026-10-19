#include "render/PitchMarks.h"

#include <algorithm>
#include <cmath>

namespace koenote::render
{
namespace
{
// 時刻 sec における F0（Hz）。最も近いフレームが無声なら 0。
float f0At(const model::PitchTrack& track, double sec) noexcept
{
    if (track.empty())
        return 0.0f;

    const auto frame = std::llround((sec - track.startSec) / track.hopSec);

    if (frame < 0 || static_cast<std::size_t>(frame) >= track.f0Hz.size())
        return 0.0f;

    return track.f0Hz[static_cast<std::size_t>(frame)];
}

// 平滑化した信号（ノイズによる偽のピークを減らす）
std::vector<float> smooth(const float* x, std::size_t n, double sampleRate)
{
    const int half = std::max(1, static_cast<int>(sampleRate / 8000.0));
    std::vector<float> y(n);
    double sum = 0.0;
    std::size_t lo = 0;
    std::size_t hi = 0; // [lo, hi)

    for (std::size_t i = 0; i < n; ++i)
    {
        const std::size_t newHi = std::min(n, i + static_cast<std::size_t>(half) + 1);
        const std::size_t newLo = i >= static_cast<std::size_t>(half) ? i - static_cast<std::size_t>(half) : 0;

        while (hi < newHi)
            sum += x[hi++];
        while (lo < newLo)
            sum -= x[lo++];

        y[i] = static_cast<float>(sum / static_cast<double>(hi - lo));
    }

    return y;
}

struct Run
{
    std::size_t firstFrame;
    std::size_t lastFrame; // 含む
};

// 有声フレームの連続区間。2 フレーム以下の欠落は橋渡しする。
std::vector<Run> voicedRuns(const model::PitchTrack& track)
{
    std::vector<Run> runs;
    const std::size_t n = track.size();
    std::size_t i = 0;

    while (i < n)
    {
        if (! track.isVoiced(i))
        {
            ++i;
            continue;
        }

        std::size_t last = i;
        std::size_t j = i + 1;

        while (j < n && j <= last + 3)
        {
            if (track.isVoiced(j))
                last = j;
            ++j;
        }

        runs.push_back({ i, last });
        i = last + 1;
    }

    return runs;
}
} // namespace

std::vector<PitchMark> findPitchMarks(const float* mono,
                                      std::size_t numSamples,
                                      double sampleRate,
                                      const model::PitchTrack& track,
                                      const PitchMarkOptions& options)
{
    std::vector<PitchMark> marks;

    if (mono == nullptr || numSamples == 0 || sampleRate <= 0.0)
        return marks;

    const auto y = smooth(mono, numSamples, sampleRate);
    const auto unvoicedStep = std::max<std::int64_t>(1, std::llround(options.unvoicedPeriodSec * sampleRate));
    const auto last = static_cast<std::int64_t>(numSamples) - 1;

    // 有声区間ごとのマークを先に求める
    std::vector<PitchMark> voicedMarks;

    for (const auto& run : voicedRuns(track))
    {
        const double startSec = track.timeOf(run.firstFrame);
        const double endSec = track.timeOf(run.lastFrame);

        const float f0Start = f0At(track, startSec);
        if (f0Start <= 0.0f)
            continue;

        const double period0 = sampleRate / f0Start;
        const auto begin = std::clamp<std::int64_t>(std::llround(startSec * sampleRate), 0, last);
        const auto endLimit = std::clamp<std::int64_t>(std::llround(endSec * sampleRate), 0, last);

        // ピークの極性（正負どちらが大きいか）を最初の周期から決める
        const auto firstEnd = std::min<std::int64_t>(last, begin + static_cast<std::int64_t>(1.2 * period0));
        float maxPos = 0.0f, maxNeg = 0.0f;
        for (auto k = begin; k <= firstEnd; ++k)
        {
            maxPos = std::max(maxPos, y[static_cast<std::size_t>(k)]);
            maxNeg = std::max(maxNeg, -y[static_cast<std::size_t>(k)]);
        }
        const float polarity = maxPos >= maxNeg ? 1.0f : -1.0f;

        auto peakIn = [&] (std::int64_t lo, std::int64_t hi)
        {
            lo = std::clamp<std::int64_t>(lo, 0, last);
            hi = std::clamp<std::int64_t>(hi, lo, last);
            auto best = lo;
            float bestValue = polarity * y[static_cast<std::size_t>(lo)];

            for (auto k = lo + 1; k <= hi; ++k)
            {
                const float v = polarity * y[static_cast<std::size_t>(k)];
                if (v > bestValue)
                {
                    bestValue = v;
                    best = k;
                }
            }

            return best;
        };

        auto position = peakIn(begin, begin + static_cast<std::int64_t>(1.2 * period0));
        voicedMarks.push_back({ position, true });

        while (true)
        {
            const double f0 = f0At(track, static_cast<double>(position) / sampleRate);
            const double period = sampleRate / (f0 > 0.0f ? f0 : f0Start);
            const auto expected = position + std::max<std::int64_t>(1, std::llround(period));

            if (expected > endLimit + static_cast<std::int64_t>(0.5 * period))
                break;

            const auto radius = std::max<std::int64_t>(1, std::llround(options.snapRatio * period));
            auto next = peakIn(expected - radius, expected + radius);

            if (next <= position)
                next = expected;

            if (next > last)
                break;

            voicedMarks.push_back({ next, true });
            position = next;
        }
    }

    // 有声マークの間を、無声マークで一定間隔に埋める
    std::int64_t cursor = 0;
    std::size_t v = 0;

    marks.push_back({ 0, false });
    if (! voicedMarks.empty() && voicedMarks.front().position == 0)
    {
        marks.back().voiced = true;
        ++v;
    }

    cursor = 0;

    while (cursor < last)
    {
        if (v < voicedMarks.size())
        {
            const auto target = voicedMarks[v].position;

            // 次の有声マークまで無声マークを置く。直前に近すぎる場合は置かない。
            while (cursor + unvoicedStep + unvoicedStep / 2 < target)
            {
                cursor += unvoicedStep;
                marks.push_back({ cursor, false });
            }

            if (target > cursor)
            {
                marks.push_back(voicedMarks[v]);
                cursor = target;
            }

            ++v;
        }
        else
        {
            cursor += unvoicedStep;
            if (cursor > last)
                cursor = last;
            marks.push_back({ cursor, false });
        }
    }

    return marks;
}
} // namespace koenote::render
