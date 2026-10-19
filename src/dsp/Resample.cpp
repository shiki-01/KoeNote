#include "dsp/Resample.h"

#include <algorithm>
#include <cmath>

namespace koenote::dsp
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kZeroCrossings = 16;   // 片側の sinc のゼロ交差数
constexpr int kPhases = 512;         // 小数位置の分解能（線形補間で補う）

// 窓付き sinc（Blackman 窓）。x は「ゼロ交差単位」の位置。
double kernel(double x) noexcept
{
    const double ax = std::abs(x);
    if (ax >= kZeroCrossings)
        return 0.0;

    const double sinc = ax < 1.0e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
    const double t = ax / kZeroCrossings; // 0..1
    const double window = 0.42 + 0.5 * std::cos(kPi * t) + 0.08 * std::cos(2.0 * kPi * t);
    return sinc * window;
}
} // namespace

std::vector<float> resample(const std::vector<float>& in, double srcRate, double dstRate)
{
    if (in.empty() || ! (srcRate > 0.0) || ! (dstRate > 0.0))
        return {};

    if (std::abs(srcRate - dstRate) < 1.0e-9)
        return in;

    const double ratio = srcRate / dstRate; // 出力 1 サンプルあたりの入力サンプル数
    const double cutoff = std::min(1.0, dstRate / srcRate); // ダウンサンプル時は出力の Nyquist に合わせる
    const double support = kZeroCrossings / cutoff;          // 入力サンプル単位の片側の幅

    // kernel(x * cutoff) の表。位置は入力サンプル単位で [-support, support] を kPhases 分割する。
    const int halfTaps = static_cast<int>(std::ceil(support));
    const int numTaps = 2 * halfTaps + 1;

    // 小数位置 phase（0..1）ごとの係数表。係数は DC ゲイン 1 に正規化しない（ダウンサンプル時は cutoff を掛ける）。
    std::vector<float> table(static_cast<std::size_t>((kPhases + 1) * numTaps));
    for (int p = 0; p <= kPhases; ++p)
    {
        const double frac = static_cast<double>(p) / kPhases;

        for (int k = 0; k < numTaps; ++k)
        {
            const double offset = static_cast<double>(k - halfTaps) - frac; // 入力サンプル位置 - 出力位置
            table[static_cast<std::size_t>(p * numTaps + k)] = static_cast<float>(cutoff * kernel(offset * cutoff));
        }
    }

    const auto outSize = static_cast<std::size_t>(std::llround(static_cast<double>(in.size()) / ratio));
    std::vector<float> out(outSize, 0.0f);
    const auto n = static_cast<std::int64_t>(in.size());

    for (std::size_t o = 0; o < outSize; ++o)
    {
        const double pos = static_cast<double>(o) * ratio;
        const auto base = static_cast<std::int64_t>(std::floor(pos));
        const double frac = pos - static_cast<double>(base);

        const double scaled = frac * kPhases;
        const auto p0 = static_cast<int>(scaled);
        const float mix = static_cast<float>(scaled - p0);
        const float* c0 = &table[static_cast<std::size_t>(p0 * numTaps)];
        const float* c1 = &table[static_cast<std::size_t>(std::min(p0 + 1, kPhases) * numTaps)];

        double acc = 0.0;
        for (int k = 0; k < numTaps; ++k)
        {
            const auto idx = base + (k - halfTaps);
            if (idx < 0 || idx >= n)
                continue;

            const float c = c0[k] + (c1[k] - c0[k]) * mix;
            acc += static_cast<double>(c) * static_cast<double>(in[static_cast<std::size_t>(idx)]);
        }

        out[o] = static_cast<float>(acc);
    }

    return out;
}
} // namespace koenote::dsp
