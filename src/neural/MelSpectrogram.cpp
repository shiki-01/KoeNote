#include "neural/MelSpectrogram.h"

#include "dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace koenote::neural
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kFSp = 200.0 / 3.0;
constexpr double kMinLogHz = 1000.0;
constexpr double kMinLogMel = kMinLogHz / kFSp;
const double kLogStep = std::log(6.4) / 27.0;

// 反射パディングした位置の添字
std::size_t reflectIndex(std::ptrdiff_t i, std::size_t n) noexcept
{
    const auto len = static_cast<std::ptrdiff_t>(n);
    if (len <= 1)
        return 0;

    const auto period = 2 * (len - 1);
    i %= period;
    if (i < 0)
        i += period;
    if (i >= len)
        i = period - i;
    return static_cast<std::size_t>(i);
}
} // namespace

double hzToMel(double hz) noexcept
{
    if (hz < kMinLogHz)
        return hz / kFSp;
    return kMinLogMel + std::log(hz / kMinLogHz) / kLogStep;
}

double melToHz(double mel) noexcept
{
    if (mel < kMinLogMel)
        return mel * kFSp;
    return kMinLogHz * std::exp(kLogStep * (mel - kMinLogMel));
}

std::vector<double> melBinCentreHz(const MelConfig& config)
{
    // librosa.filters.mel と同じ: fmin..fmax をメル尺度で numMelBins + 2 点に等分する
    const double melMin = hzToMel(config.fmin);
    const double melMax = hzToMel(config.fmax);
    std::vector<double> centres(static_cast<std::size_t>(config.numMelBins));

    for (int b = 0; b < config.numMelBins; ++b)
        centres[static_cast<std::size_t>(b)] = melToHz(melMin + (melMax - melMin) * (b + 1) / (config.numMelBins + 1));

    return centres;
}

void shiftMelFrame(const float* in, float* out, const std::vector<double>& centreHz, double factor)
{
    const auto n = centreHz.size();

    if (n == 0)
        return;

    for (std::size_t b = 0; b < n; ++b)
    {
        const double sourceHz = centreHz[b] / factor;

        // sourceHz を挟む 2 つのビンを探す（centreHz は昇順）
        if (sourceHz <= centreHz.front())
        {
            out[b] = in[0];
        }
        else if (sourceHz >= centreHz.back())
        {
            out[b] = in[n - 1];
        }
        else
        {
            const auto it = std::upper_bound(centreHz.begin(), centreHz.end(), sourceHz);
            const auto hi = static_cast<std::size_t>(it - centreHz.begin());
            const auto lo = hi - 1;
            const double t = (sourceHz - centreHz[lo]) / (centreHz[hi] - centreHz[lo]);
            out[b] = static_cast<float>(in[lo] + (in[hi] - in[lo]) * t);
        }
    }
}

std::vector<float> computeLogMel(const std::vector<float>& wave, const MelConfig& config, std::size_t& numFrames)
{
    numFrames = 0;

    if (wave.empty() || config.hopSize <= 0 || config.fftSize < config.winSize)
        return {};

    const auto hop = static_cast<std::size_t>(config.hopSize);
    const auto fftSize = static_cast<std::size_t>(config.fftSize);
    const auto winSize = static_cast<std::size_t>(config.winSize);
    const std::ptrdiff_t pad = static_cast<std::ptrdiff_t>((fftSize - hop) / 2);

    numFrames = (wave.size() + hop - 1) / hop;
    const auto bins = static_cast<std::size_t>(config.numMelBins);
    const auto half = fftSize / 2 + 1;

    // メルフィルタバンク（Slaney 正規化）
    const double melMin = hzToMel(config.fmin);
    const double melMax = hzToMel(config.fmax);
    std::vector<double> edges(bins + 2);
    for (std::size_t i = 0; i < edges.size(); ++i)
        edges[i] = melToHz(melMin + (melMax - melMin) * static_cast<double>(i) / static_cast<double>(bins + 1));

    std::vector<std::vector<std::pair<std::size_t, float>>> filters(bins); // 疎なフィルタ（ビン番号, 重み）
    for (std::size_t b = 0; b < bins; ++b)
    {
        const double lower = edges[b], centre = edges[b + 1], upper = edges[b + 2];
        const double norm = 2.0 / (upper - lower);

        for (std::size_t k = 0; k < half; ++k)
        {
            const double f = static_cast<double>(k) * config.sampleRate / static_cast<double>(fftSize);
            const double w = std::max(0.0, std::min((f - lower) / (centre - lower), (upper - f) / (upper - centre)));

            if (w > 0.0)
                filters[b].emplace_back(k, static_cast<float>(w * norm));
        }
    }

    // 周期 Hann 窓（torch.hann_window の既定）。win が fft より短い場合は中央に置く。
    std::vector<double> window(fftSize, 0.0);
    const auto offset = (fftSize - winSize) / 2;
    for (std::size_t i = 0; i < winSize; ++i)
        window[offset + i] = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(winSize));

    const dsp::Fft fft(fftSize);
    std::vector<std::complex<double>> buffer(fftSize);
    std::vector<double> magnitude(half);
    std::vector<float> mel(numFrames * bins);

    for (std::size_t j = 0; j < numFrames; ++j)
    {
        // 窓の開始位置 = j*hop - pad（反射パディングの座標）
        const auto start = static_cast<std::ptrdiff_t>(j * hop) - pad;

        for (std::size_t i = 0; i < fftSize; ++i)
            buffer[i] = window[i] * static_cast<double>(wave[reflectIndex(start + static_cast<std::ptrdiff_t>(i), wave.size())]);

        fft.forward(buffer.data());

        for (std::size_t k = 0; k < half; ++k)
            magnitude[k] = std::sqrt(std::norm(buffer[k]) + 1.0e-9);

        for (std::size_t b = 0; b < bins; ++b)
        {
            double sum = 0.0;
            for (const auto& [k, w] : filters[b])
                sum += static_cast<double>(w) * magnitude[k];

            mel[j * bins + b] = static_cast<float>(std::log(std::max(sum, 1.0e-5)));
        }
    }

    return mel;
}
} // namespace koenote::neural
