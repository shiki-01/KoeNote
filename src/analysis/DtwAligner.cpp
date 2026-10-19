#include "analysis/DtwAligner.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

namespace koenote::analysis
{
namespace
{
constexpr double kPi = std::numbers::pi;
constexpr double kFrameLenSec = 0.025;   // 解析窓長（Hann 窓）
constexpr double kPathStepSec = 0.05;    // 経路の間引き間隔
constexpr double kLogFloor = 1e-10;      // 対数の下限。無音フレームの特徴量を安定させる
constexpr double kStdFloor = 1e-9;       // 標準偏差がこれ未満の次元は分散で割らない
constexpr std::size_t kMinFrames = 10;
constexpr std::size_t kProgressInterval = 64;
constexpr double kDiagonalWeight = 2.0;  // 対角ステップは水平・垂直の 2 倍（長さの偏り防止）
constexpr double kInf = std::numeric_limits<double>::infinity();

using Complex = std::complex<double>;
using Step = std::pair<std::size_t, std::size_t>;

// 特徴量系列（frames x dim の行優先配列）
struct Features
{
    std::size_t frames = 0;
    std::size_t dim = 0;
    double timeOffsetSec = 0.0; // フレーム 0 の窓中心時刻
    double timeStepSec = 0.0;   // 実際のフレーム間隔
    std::vector<double> data;

    double timeOf(std::size_t i) const
    {
        return timeOffsetSec + static_cast<double>(i) * timeStepSec;
    }
    const double* row(std::size_t i) const
    {
        return data.data() + i * dim;
    }
};

// Sakoe-Chiba 帯。行 i（guide フレーム）の有効列は [lo[i], hi[i]]。セルは行ごとに連続して格納する。
struct Band
{
    std::vector<std::size_t> lo;
    std::vector<std::size_t> hi;
    std::vector<std::size_t> offset;
    std::size_t totalCells = 0;

    bool contains(std::size_t i, std::size_t j) const
    {
        return j >= lo[i] && j <= hi[i];
    }
};

bool isValidAudio(const MonoAudioView& audio)
{
    return audio.samples != nullptr && audio.numSamples > 0 && std::isfinite(audio.sampleRate)
        && audio.sampleRate > 0.0;
}

bool isValidOptions(const AlignmentOptions& o)
{
    return std::isfinite(o.hopSec) && o.hopSec > 0.0 && o.numMelBands >= 2 && o.numMfcc >= 1
        && o.numMfcc <= o.numMelBands && std::isfinite(o.bandRadiusSec) && o.bandRadiusSec >= 0.0
        && std::isfinite(o.maxSampleRate) && o.maxSampleRate > 0.0;
}

std::size_t nextPow2(std::size_t n)
{
    std::size_t p = 1;
    while (p < n)
    {
        p <<= 1;
    }
    return p;
}

// 自前の radix-2 FFT（サイズは 2 のべき乗）
class Fft
{
public:
    explicit Fft(std::size_t n) : n_(n), bitrev_(n), twiddle_(n / 2)
    {
        for (std::size_t i = 1; i < n; ++i)
        {
            bitrev_[i] = (bitrev_[i >> 1] >> 1) | (((i & 1) != 0) ? (n >> 1) : 0);
        }
        for (std::size_t k = 0; k < n / 2; ++k)
        {
            twiddle_[k] = std::polar(1.0, -2.0 * kPi * static_cast<double>(k) / static_cast<double>(n));
        }
    }

    void transform(std::vector<Complex>& a) const
    {
        for (std::size_t i = 0; i < n_; ++i)
        {
            if (i < bitrev_[i])
            {
                std::swap(a[i], a[bitrev_[i]]);
            }
        }
        for (std::size_t len = 2; len <= n_; len <<= 1)
        {
            const std::size_t half = len >> 1;
            const std::size_t step = n_ / len;
            for (std::size_t i = 0; i < n_; i += len)
            {
                for (std::size_t k = 0; k < half; ++k)
                {
                    const Complex t = a[i + k + half] * twiddle_[k * step];
                    const Complex u = a[i + k];
                    a[i + k] = u + t;
                    a[i + k + half] = u - t;
                }
            }
        }
    }

private:
    std::size_t n_;
    std::vector<std::size_t> bitrev_;
    std::vector<Complex> twiddle_;
};

// メル帯域の三角フィルタ（numBands x (fftSize/2+1) の行優先配列）
std::vector<double> buildMelFilterBank(std::size_t numBands, std::size_t fftSize, double sampleRate)
{
    const std::size_t bins = fftSize / 2 + 1;
    const auto hzToMel = [](double hz) { return 2595.0 * std::log10(1.0 + hz / 700.0); };
    const auto melToHz = [](double mel) { return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0); };

    const double melMax = hzToMel(sampleRate / 2.0);
    std::vector<double> edges(numBands + 2);
    for (std::size_t m = 0; m < edges.size(); ++m)
    {
        edges[m] = melToHz(melMax * static_cast<double>(m) / static_cast<double>(numBands + 1));
    }

    const double binHz = sampleRate / static_cast<double>(fftSize);
    std::vector<double> weights(numBands * bins, 0.0);
    for (std::size_t m = 0; m < numBands; ++m)
    {
        const double lo = edges[m];
        const double mid = edges[m + 1];
        const double hi = edges[m + 2];
        for (std::size_t k = 0; k < bins; ++k)
        {
            const double f = static_cast<double>(k) * binHz;
            if (f > lo && f <= mid)
            {
                weights[m * bins + k] = (f - lo) / (mid - lo);
            }
            else if (f > mid && f < hi)
            {
                weights[m * bins + k] = (hi - f) / (hi - mid);
            }
        }
    }
    return weights;
}

// 音声から MFCC（c0 を除く numMfcc 個）と対数エネルギーを計算し、次元ごとに標準化する。
// progress が false を返した場合は false を返す。入力が短すぎる場合は frames = 0 のまま true を返す。
bool extractFeatures(const MonoAudioView& audio, const AlignmentOptions& opt, const ProgressCallback& progress,
                     float progressBase, float progressSpan, Features& out)
{
    out = Features{};

    // 整数倍間引き。間引き後のサンプルレートが maxSampleRate 以下になるよう切り上げる
    const std::size_t factor =
        static_cast<std::size_t>(std::max(1.0, std::ceil(audio.sampleRate / opt.maxSampleRate - 1e-9)));
    const double fe = audio.sampleRate / static_cast<double>(factor);
    const std::size_t n = audio.numSamples / factor;
    const std::size_t win = std::max<std::size_t>(2, static_cast<std::size_t>(std::llround(kFrameLenSec * fe)));
    const std::size_t hop = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(opt.hopSec * fe)));
    if (n < win)
    {
        return true;
    }
    const std::size_t frames = (n - win) / hop + 1;

    // 区間平均による簡易ローパスを兼ねた間引き
    std::vector<double> x(n);
    for (std::size_t k = 0; k < n; ++k)
    {
        double acc = 0.0;
        for (std::size_t q = 0; q < factor; ++q)
        {
            acc += static_cast<double>(audio.samples[k * factor + q]);
        }
        x[k] = acc / static_cast<double>(factor);
    }

    std::vector<double> window(win);
    for (std::size_t i = 0; i < win; ++i)
    {
        window[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(win - 1));
    }

    const std::size_t fftSize = nextPow2(win);
    const Fft fft(fftSize);
    const std::size_t bins = fftSize / 2 + 1;
    const std::size_t nb = static_cast<std::size_t>(opt.numMelBands);
    const std::size_t numMfcc = static_cast<std::size_t>(opt.numMfcc);
    const std::vector<double> melWeights = buildMelFilterBank(nb, fftSize, fe);

    // DCT-II の基底（k = 1..numMfcc。c0 は使わない）
    std::vector<double> dct(numMfcc * nb);
    for (std::size_t k = 1; k <= numMfcc; ++k)
    {
        for (std::size_t m = 0; m < nb; ++m)
        {
            dct[(k - 1) * nb + m] = std::cos(kPi * static_cast<double>(k) * (static_cast<double>(m) + 0.5)
                                              / static_cast<double>(nb));
        }
    }

    out.frames = frames;
    out.dim = numMfcc + 1;
    out.timeOffsetSec = (static_cast<double>(win) / 2.0) / fe;
    out.timeStepSec = static_cast<double>(hop) / fe;
    out.data.assign(frames * out.dim, 0.0);

    std::vector<Complex> buf(fftSize);
    std::vector<double> logMel(nb);
    for (std::size_t f = 0; f < frames; ++f)
    {
        const std::size_t start = f * hop;
        double energy = 0.0;
        for (std::size_t i = 0; i < fftSize; ++i)
        {
            double v = 0.0;
            if (i < win)
            {
                v = x[start + i] * window[i];
                energy += v * v;
            }
            buf[i] = Complex(v, 0.0);
        }
        fft.transform(buf);

        for (std::size_t m = 0; m < nb; ++m)
        {
            double e = 0.0;
            for (std::size_t k = 0; k < bins; ++k)
            {
                e += melWeights[m * bins + k] * std::norm(buf[k]);
            }
            logMel[m] = std::log(std::max(e, kLogFloor));
        }

        double* row = out.data.data() + f * out.dim;
        for (std::size_t k = 0; k < numMfcc; ++k)
        {
            double s = 0.0;
            for (std::size_t m = 0; m < nb; ++m)
            {
                s += logMel[m] * dct[k * nb + m];
            }
            row[k] = s;
        }
        row[numMfcc] = std::log(std::max(energy, kLogFloor));

        if (progress && f % kProgressInterval == 0
            && !progress(progressBase + progressSpan * static_cast<float>(f) / static_cast<float>(frames)))
        {
            return false;
        }
    }

    // 次元ごとの平均・分散による標準化（系列全体で計算）
    for (std::size_t d = 0; d < out.dim; ++d)
    {
        double mean = 0.0;
        for (std::size_t f = 0; f < frames; ++f)
        {
            mean += out.data[f * out.dim + d];
        }
        mean /= static_cast<double>(frames);

        double var = 0.0;
        for (std::size_t f = 0; f < frames; ++f)
        {
            const double diff = out.data[f * out.dim + d] - mean;
            var += diff * diff;
        }
        double sd = std::sqrt(var / static_cast<double>(frames));
        if (sd < kStdFloor)
        {
            sd = 1.0;
        }
        for (std::size_t f = 0; f < frames; ++f)
        {
            out.data[f * out.dim + d] = (out.data[f * out.dim + d] - mean) / sd;
        }
    }
    return true;
}

// 行 i の帯を作る。半径は行の連結と終端への到達に必要な値まで広げる。
Band makeBand(std::size_t ng, std::size_t nt, double radiusFrames)
{
    const double rho = static_cast<double>(nt) / static_cast<double>(ng);
    const double r = std::max(radiusFrames, std::ceil(rho) + 1.0);

    Band b;
    b.lo.resize(ng);
    b.hi.resize(ng);
    b.offset.resize(ng);
    for (std::size_t i = 0; i < ng; ++i)
    {
        const double c = static_cast<double>(i) * rho;
        const double loD = std::ceil(c - r);
        const double hiD = std::floor(c + r);
        b.lo[i] = (loD <= 0.0) ? 0 : std::min(nt - 1, static_cast<std::size_t>(loD));
        b.hi[i] = (hiD >= static_cast<double>(nt - 1)) ? nt - 1 : static_cast<std::size_t>(hiD);
        b.offset[i] = b.totalCells;
        b.totalCells += b.hi[i] - b.lo[i] + 1;
    }
    return b;
}

// フレーム間のユークリッド距離
double frameDistance(const Features& g, std::size_t i, const Features& t, std::size_t j)
{
    const double* a = g.row(i);
    const double* b = t.row(j);
    double s = 0.0;
    for (std::size_t d = 0; d < g.dim; ++d)
    {
        const double diff = a[d] - b[d];
        s += diff * diff;
    }
    return std::sqrt(s);
}

// 帯付き DTW の累積コストを計算し、バックトレースで経路（フレーム対の列）を得る。
// バックトレースでは方向を保存せず、累積コストから再計算する（メモリ節約）。
// progress が false を返した場合、または経路が得られない場合は false を返す。
bool solveDtw(const Features& g, const Features& t, const Band& band, const ProgressCallback& progress,
              std::vector<Step>& path)
{
    const std::size_t ng = g.frames;
    const std::size_t nt = t.frames;
    std::vector<float> cells(band.totalCells);
    std::vector<double> prev;
    std::vector<double> cur;

    for (std::size_t i = 0; i < ng; ++i)
    {
        const std::size_t lo = band.lo[i];
        const std::size_t hi = band.hi[i];
        const std::size_t pLo = (i > 0) ? band.lo[i - 1] : 0;
        const std::size_t pHi = (i > 0) ? band.hi[i - 1] : 0;
        cur.assign(hi - lo + 1, kInf);

        for (std::size_t j = lo; j <= hi; ++j)
        {
            const double c = frameDistance(g, i, t, j);
            double acc = kInf;
            if (i == 0 && j == 0)
            {
                acc = c;
            }
            else
            {
                // 対角 (i-1, j-1)、垂直 (i-1, j)、水平 (i, j-1) の順で評価する
                if (i > 0 && j > 0 && j - 1 >= pLo && j - 1 <= pHi)
                {
                    acc = std::min(acc, prev[j - 1 - pLo] + kDiagonalWeight * c);
                }
                if (i > 0 && j >= pLo && j <= pHi)
                {
                    acc = std::min(acc, prev[j - pLo] + c);
                }
                if (j > lo)
                {
                    acc = std::min(acc, cur[j - 1 - lo] + c);
                }
            }
            cur[j - lo] = acc;
            cells[band.offset[i] + j - lo] = static_cast<float>(acc);
        }
        std::swap(prev, cur);

        if (progress && i % kProgressInterval == 0
            && !progress(0.1f + 0.85f * static_cast<float>(i) / static_cast<float>(ng)))
        {
            return false;
        }
    }

    const auto cellAt = [&](std::size_t i, std::size_t j) -> double {
        if (!band.contains(i, j))
        {
            return kInf;
        }
        return static_cast<double>(cells[band.offset[i] + j - band.lo[i]]);
    };

    if (!std::isfinite(cellAt(ng - 1, nt - 1)))
    {
        return false;
    }

    path.clear();
    std::size_t i = ng - 1;
    std::size_t j = nt - 1;
    path.emplace_back(i, j);
    while (i > 0 || j > 0)
    {
        const double c = frameDistance(g, i, t, j);
        double best = kInf;
        std::size_t bi = i;
        std::size_t bj = j;
        if (i > 0 && j > 0)
        {
            const double v = cellAt(i - 1, j - 1) + kDiagonalWeight * c;
            if (v < best)
            {
                best = v;
                bi = i - 1;
                bj = j - 1;
            }
        }
        if (i > 0)
        {
            const double v = cellAt(i - 1, j) + c;
            if (v < best)
            {
                best = v;
                bi = i - 1;
                bj = j;
            }
        }
        if (j > 0)
        {
            const double v = cellAt(i, j - 1) + c;
            if (v < best)
            {
                best = v;
                bi = i;
                bj = j - 1;
            }
        }
        if (!std::isfinite(best))
        {
            return false;
        }
        i = bi;
        j = bj;
        path.emplace_back(i, j);
    }
    std::reverse(path.begin(), path.end());
    return true;
}

// 経路を約 50 ms 間隔に間引く。端点は必ず含める。入力経路が単調なので結果も単調になる。
std::vector<AlignmentAnchor> thinPath(const Features& g, const Features& t, const std::vector<Step>& path)
{
    std::vector<AlignmentAnchor> out;
    for (std::size_t k = 0; k < path.size(); ++k)
    {
        const AlignmentAnchor a{ g.timeOf(path[k].first), t.timeOf(path[k].second) };
        if (out.empty())
        {
            out.push_back(a);
            continue;
        }
        const AlignmentAnchor& last = out.back();
        const bool isEnd = (k + 1 == path.size());
        const bool advanced = a.guideSec - last.guideSec >= kPathStepSec || a.targetSec - last.targetSec >= kPathStepSec;
        if ((isEnd || advanced) && (a.guideSec != last.guideSec || a.targetSec != last.targetSec))
        {
            out.push_back(a);
        }
    }
    return out;
}

// path を区分線形に写す。key は写像の定義域側、value は値域側の時刻（いずれも単調非減少）。
template <typename KeyOf, typename ValueOf>
double mapPiecewise(const std::vector<AlignmentAnchor>& path, double x, KeyOf key, ValueOf value)
{
    if (path.empty())
    {
        return x;
    }
    if (x <= key(path.front()))
    {
        return value(path.front()) + (x - key(path.front()));
    }
    if (x >= key(path.back()))
    {
        return value(path.back()) + (x - key(path.back()));
    }

    // key(front) < x < key(back) なので、first は begin より後ろ
    const auto it = std::lower_bound(path.begin(), path.end(), x,
                                     [&](const AlignmentAnchor& a, double v) { return key(a) < v; });
    if (key(*it) == x)
    {
        return value(*it);
    }
    const auto prev = it - 1;
    const double ratio = (x - key(*prev)) / (key(*it) - key(*prev)); // 分母は正
    return value(*prev) + ratio * (value(*it) - value(*prev));
}
} // namespace

double AlignmentResult::mapTargetToGuide(double targetSec) const
{
    return mapPiecewise(
        path, targetSec, [](const AlignmentAnchor& a) { return a.targetSec; },
        [](const AlignmentAnchor& a) { return a.guideSec; });
}

double AlignmentResult::mapGuideToTarget(double guideSec) const
{
    return mapPiecewise(
        path, guideSec, [](const AlignmentAnchor& a) { return a.guideSec; },
        [](const AlignmentAnchor& a) { return a.targetSec; });
}

AlignmentResult alignAudio(const MonoAudioView& guide, const MonoAudioView& target, const AlignmentOptions& options,
                           const ProgressCallback& progress)
{
    AlignmentResult result;
    if (!isValidAudio(guide) || !isValidAudio(target) || !isValidOptions(options))
    {
        return result;
    }

    // 特徴量の進捗は 0〜0.1 を guide と target で分け合う
    Features g;
    Features t;
    if (!extractFeatures(guide, options, progress, 0.0f, 0.05f, g))
    {
        return result;
    }
    if (!extractFeatures(target, options, progress, 0.05f, 0.05f, t))
    {
        return result;
    }
    if (g.frames < kMinFrames || t.frames < kMinFrames)
    {
        return result;
    }

    const Band band = makeBand(g.frames, t.frames, options.bandRadiusSec / options.hopSec);
    std::vector<Step> path;
    if (!solveDtw(g, t, band, progress, path))
    {
        return result;
    }

    result.path = thinPath(g, t, path);
    result.valid = result.path.size() >= 2;
    if (!result.valid)
    {
        result.path.clear();
    }
    return result;
}
} // namespace koenote::analysis
