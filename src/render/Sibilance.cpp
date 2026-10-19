#include "render/Sibilance.h"

#include <algorithm>
#include <cmath>

namespace koenote::render
{
std::vector<float> detectSibilance(const float* mono, std::size_t numFrames, double sampleRate, const model::PitchTrack& track)
{
    std::vector<float> amount(track.size(), 0.0f);

    if (mono == nullptr || numFrames == 0 || track.empty() || sampleRate < 12000.0 || ! (track.hopSec > 0.0))
        return amount;

    // 4 kHz の 2 次ハイパス（RBJ の biquad、Q = 0.707）
    constexpr double kPi = 3.14159265358979323846;
    const double w0 = 2.0 * kPi * 4000.0 / sampleRate;
    const double alpha = std::sin(w0) / (2.0 * 0.7071);
    const double cosw = std::cos(w0);
    const double a0 = 1.0 + alpha;
    const double b0 = (1.0 + cosw) * 0.5 / a0;
    const double b1 = -(1.0 + cosw) / a0;
    const double b2 = b0;
    const double a1 = -2.0 * cosw / a0;
    const double a2 = (1.0 - alpha) / a0;

    // フレーム（hop 幅のビン）ごとの全体と高域のエネルギー
    const std::size_t bins = track.size();
    std::vector<double> total(bins, 0.0), high(bins, 0.0);
    std::vector<std::size_t> counts(bins, 0);

    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    for (std::size_t i = 0; i < numFrames; ++i)
    {
        const double x = mono[i];
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;

        const double sec = static_cast<double>(i) / sampleRate;
        const auto bin = std::llround((sec - track.startSec) / track.hopSec);

        if (bin < 0 || static_cast<std::size_t>(bin) >= bins)
            continue;

        total[static_cast<std::size_t>(bin)] += x * x;
        high[static_cast<std::size_t>(bin)] += y * y;
        ++counts[static_cast<std::size_t>(bin)];
    }

    // 前後 1 ビンを合わせた窓（約 15 ms）で判定する
    std::vector<float> raw(bins, 0.0f);

    for (std::size_t f = 0; f < bins; ++f)
    {
        if (track.isVoiced(f))
            continue;

        double t = 0.0, h = 0.0;
        std::size_t n = 0;

        for (std::size_t k = f > 0 ? f - 1 : 0; k <= std::min(bins - 1, f + 1); ++k)
        {
            t += total[k];
            h += high[k];
            n += counts[k];
        }

        if (n == 0 || ! (t > 0.0))
            continue;

        const double rms = std::sqrt(t / static_cast<double>(n));

        if (rms < 0.003) // 約 -50 dBFS 未満は無音・ノイズとみなす
            continue;

        const double ratio = h / t;
        raw[f] = static_cast<float>(std::clamp((ratio - 0.35) / 0.25, 0.0, 1.0));
    }

    // 3 フレームの移動平均で、判定の細かい揺れをならす
    for (std::size_t f = 0; f < bins; ++f)
    {
        double sum = 0.0;
        int n = 0;

        for (std::size_t k = f > 0 ? f - 1 : 0; k <= std::min(bins - 1, f + 1); ++k)
        {
            sum += raw[k];
            ++n;
        }

        amount[f] = static_cast<float>(sum / n);
    }

    return amount;
}

bool hasSibilanceEdits(const std::vector<model::Note>& notes) noexcept
{
    return std::any_of(notes.begin(), notes.end(), [] (const model::Note& n) { return n.edit.sibilanceGainDb != 0.0f; });
}

int sibilanceOwner(const std::vector<model::Note>& notes, double sourceSec) noexcept
{
    if (notes.empty())
        return -1;

    // sourceSec より後に始まる最初のノート
    const auto next = std::upper_bound(notes.begin(), notes.end(), sourceSec,
                                       [] (double sec, const model::Note& n) { return sec < n.startSec; });

    // ノートの中にあれば、そのノート
    if (next != notes.begin() && sourceSec < std::prev(next)->endSec)
        return static_cast<int>(next - notes.begin()) - 1;

    if (next == notes.end())
        return static_cast<int>(notes.size()) - 1;

    if (next == notes.begin() || next->startSec - sourceSec <= kSibilanceLeadSec)
        return static_cast<int>(next - notes.begin());

    return static_cast<int>(next - notes.begin()) - 1;
}
} // namespace koenote::render
