#include "analysis/YinPitchAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace koenote::analysis
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTargetRateHz = 16000.0;   // 解析前に目標とするサンプルレート（整数倍間引き）
constexpr int kFirHalfLength = 31;          // FIR は 63 タップ
constexpr double kFirCutoffRatio = 0.45;    // 出力 Nyquist に対する遮断比
constexpr float kCmndThreshold = 0.45f;     // 候補とする d' の上限（exclusive）
constexpr std::size_t kMaxCandidates = 6;
constexpr std::size_t kMaxStates = kMaxCandidates + 1; // 候補 + 無声
constexpr double kUnvoicedCost = 0.35;
constexpr double kVoicingSwitchCost = 0.3;
constexpr double kOctaveTransitionWeight = 1.5;
constexpr std::size_t kProgressInterval = 100;

struct Candidate
{
    float f0Hz = 0.0f;
    float dPrime = 0.0f;
    float period = 0.0f; // decimated サンプル単位の周期（放物線補間後）
};

struct FrameCandidates
{
    std::array<Candidate, kMaxCandidates> items{};
    std::size_t count = 0;
};

// 窓付き sinc の FIR ローパスで整数倍に間引く。出力 k は入力の k * factor に対応する。
std::vector<float> decimate(const float* x, std::size_t n, std::size_t factor)
{
    if (factor <= 1)
    {
        return std::vector<float>(x, x + n);
    }

    const double fc = kFirCutoffRatio / static_cast<double>(factor); // 入力サンプル単位（cycles/sample）
    const int half = kFirHalfLength;
    std::vector<double> h(static_cast<std::size_t>(2 * half + 1));
    double sum = 0.0;
    for (int j = -half; j <= half; ++j)
    {
        const double t = static_cast<double>(j);
        const double sinc = (j == 0) ? 2.0 * fc : std::sin(2.0 * kPi * fc * t) / (kPi * t);
        const double w = 0.42 + 0.5 * std::cos(kPi * t / (half + 1)) + 0.08 * std::cos(2.0 * kPi * t / (half + 1));
        h[static_cast<std::size_t>(j + half)] = sinc * w;
        sum += sinc * w;
    }
    for (double& v : h)
    {
        v /= sum; // DC ゲインを 1 にする
    }

    const std::size_t outN = (n + factor - 1) / factor;
    std::vector<float> y(outN);
    for (std::size_t k = 0; k < outN; ++k)
    {
        const std::ptrdiff_t center = static_cast<std::ptrdiff_t>(k * factor);
        double acc = 0.0;
        for (int j = -half; j <= half; ++j)
        {
            const std::ptrdiff_t idx = center + j;
            if (idx < 0 || idx >= static_cast<std::ptrdiff_t>(n))
            {
                continue;
            }
            acc += h[static_cast<std::size_t>(j + half)] * static_cast<double>(x[idx]);
        }
        y[k] = static_cast<float>(acc);
    }
    return y;
}

// 状態 s の観測コスト。s == count は無声状態。
double observationCost(const FrameCandidates& f, std::size_t s)
{
    return s < f.count ? static_cast<double>(f.items[s].dPrime) : kUnvoicedCost;
}

// 前フレーム状態 sp から現フレーム状態 sc への遷移コスト
double transitionCost(const FrameCandidates& prev, std::size_t sp, const FrameCandidates& cur, std::size_t sc)
{
    const bool voicedPrev = sp < prev.count;
    const bool voicedCur = sc < cur.count;
    if (voicedPrev && voicedCur)
    {
        const double fp = static_cast<double>(prev.items[sp].f0Hz);
        const double fc = static_cast<double>(cur.items[sc].f0Hz);
        return kOctaveTransitionWeight * std::abs(std::log2(fp / fc));
    }
    if (voicedPrev != voicedCur)
    {
        return kVoicingSwitchCost;
    }
    return 0.0;
}
} // namespace

YinPitchAnalyzer::YinPitchAnalyzer() = default;

YinPitchAnalyzer::YinPitchAnalyzer(const Params& p) : params(p) {}

model::PitchTrack YinPitchAnalyzer::analyze(const MonoAudioView& audio, const ProgressCallback& progress)
{
    model::PitchTrack empty;
    empty.hopSec = params.hopSec;

    if (audio.samples == nullptr || audio.numSamples == 0 || !(audio.sampleRate > 0.0) || !(params.hopSec > 0.0)
        || !(params.fMinHz > 0.0f) || !(params.fMaxHz > params.fMinHz))
    {
        return empty;
    }

    // 整数倍間引き（factor >= 1）。fsd は間引き後のサンプルレート
    const std::size_t factor =
        static_cast<std::size_t>(std::max(1.0, std::round(audio.sampleRate / kTargetRateHz)));
    const double fsd = audio.sampleRate / static_cast<double>(factor);
    const std::vector<float> xd = decimate(audio.samples, audio.numSamples, factor);
    const std::ptrdiff_t nDec = static_cast<std::ptrdiff_t>(xd.size());

    const std::size_t numFrames = static_cast<std::size_t>(std::floor(audio.durationSec() / params.hopSec)) + 1;

    // YIN の窓長とラグ範囲（間引き後のサンプル単位）
    const std::ptrdiff_t W = std::max<std::ptrdiff_t>(
        4, static_cast<std::ptrdiff_t>(std::llround(1.5 / static_cast<double>(params.fMinHz) * fsd)));
    const std::ptrdiff_t halfW = W / 2;
    const std::ptrdiff_t tauLo = std::max<std::ptrdiff_t>(
        2, static_cast<std::ptrdiff_t>(std::floor(fsd / static_cast<double>(params.fMaxHz))));
    const std::ptrdiff_t tauHi = static_cast<std::ptrdiff_t>(std::ceil(fsd / static_cast<double>(params.fMinHz)));
    const std::ptrdiff_t dMax = tauHi + 1; // 局所最小の判定に 1 つ先の d' が要る
    if (tauLo > tauHi)
    {
        return empty;
    }

    // 窓の前後に 0 の余白を取り、境界判定を不要にする
    const std::ptrdiff_t margin = W + dMax + 8;
    std::vector<float> xp(static_cast<std::size_t>(nDec + 2 * margin), 0.0f);
    std::copy(xd.begin(), xd.end(), xp.begin() + margin);

    const double silenceMeanSquare = std::pow(10.0, static_cast<double>(params.silenceDb) / 10.0);

    std::vector<FrameCandidates> frames(numFrames);
    std::vector<float> d(static_cast<std::size_t>(dMax + 1));
    std::vector<float> dp(static_cast<std::size_t>(dMax + 1));
    std::vector<Candidate> found;
    std::vector<Candidate> kept;

    // フレームごとの候補抽出
    for (std::size_t i = 0; i < numFrames; ++i)
    {
        if (progress && i % kProgressInterval == 0 && !progress(static_cast<float>(i) / static_cast<float>(numFrames)))
        {
            return empty;
        }

        const double t = static_cast<double>(i) * params.hopSec;
        const std::ptrdiff_t c = static_cast<std::ptrdiff_t>(std::llround(t * fsd)); // 窓の中心（間引き後の添字）
        const float* s = xp.data() + (margin + c - halfW);

        double meanSquare = 0.0;
        for (std::ptrdiff_t j = 0; j < W; ++j)
        {
            meanSquare += static_cast<double>(s[j]) * static_cast<double>(s[j]);
        }
        meanSquare /= static_cast<double>(W);
        if (meanSquare < silenceMeanSquare)
        {
            continue; // 候補なし（無声固定）
        }

        // 差分関数 d(τ)
        for (std::ptrdiff_t tau = 1; tau <= dMax; ++tau)
        {
            const float* b = s + tau;
            float acc = 0.0f;
            for (std::ptrdiff_t j = 0; j < W; ++j)
            {
                const float diff = s[j] - b[j];
                acc += diff * diff;
            }
            d[static_cast<std::size_t>(tau)] = acc;
        }

        // 累積平均正規化差分関数 d'(τ)
        dp[0] = 1.0f;
        float running = 0.0f;
        for (std::ptrdiff_t tau = 1; tau <= dMax; ++tau)
        {
            running += d[static_cast<std::size_t>(tau)];
            dp[static_cast<std::size_t>(tau)] = running > 1e-12f
                ? d[static_cast<std::size_t>(tau)] * static_cast<float>(tau) / running
                : 1.0f;
        }

        // 局所最小（d' < 閾値）を放物線補間し、小さい τ から順に採用する。
        // 採用済みの周期の整数倍（サブハーモニック）にある候補は除外する。
        found.clear();
        kept.clear();
        for (std::ptrdiff_t tau = tauLo; tau <= tauHi; ++tau)
        {
            const std::size_t k = static_cast<std::size_t>(tau);
            const float a = dp[k - 1];
            const float b = dp[k];
            const float cc = dp[k + 1];
            if (b >= kCmndThreshold || !(b < a && b <= cc))
            {
                continue;
            }
            const float den = a - 2.0f * b + cc;
            const float offset = den > 1e-12f ? std::clamp(0.5f * (a - cc) / den, -1.0f, 1.0f) : 0.0f;
            Candidate cand;
            cand.period = static_cast<float>(tau) + offset;
            cand.f0Hz = static_cast<float>(fsd) / cand.period;
            cand.dPrime = b;

            bool subharmonic = false;
            for (const Candidate& p : kept)
            {
                const double ratio = static_cast<double>(cand.period) / static_cast<double>(p.period);
                const double m = std::round(ratio);
                if (m >= 2.0 && std::abs(static_cast<double>(cand.period) - m * static_cast<double>(p.period))
                        <= 1.0 + 0.02 * static_cast<double>(cand.period))
                {
                    subharmonic = true;
                    break;
                }
            }
            if (!subharmonic)
            {
                kept.push_back(cand);
            }
        }

        // d' の小さい順に最大 kMaxCandidates 個
        std::sort(kept.begin(), kept.end(), [](const Candidate& x, const Candidate& y) {
            return x.dPrime != y.dPrime ? x.dPrime < y.dPrime : x.period < y.period;
        });
        FrameCandidates& fc = frames[i];
        fc.count = std::min(kept.size(), kMaxCandidates);
        for (std::size_t k = 0; k < fc.count; ++k)
        {
            fc.items[k] = kept[k];
        }
    }

    // Viterbi。状態は「候補 k」（0..count-1）と「無声」（count）
    std::vector<std::array<std::uint8_t, kMaxStates>> back(numFrames);
    std::array<double, kMaxStates> cost{};
    std::array<double, kMaxStates> nextCost{};
    for (std::size_t s = 0; s <= frames[0].count; ++s)
    {
        cost[s] = observationCost(frames[0], s);
    }
    for (std::size_t i = 1; i < numFrames; ++i)
    {
        const std::size_t nPrev = frames[i - 1].count + 1;
        const std::size_t nCur = frames[i].count + 1;
        for (std::size_t sc = 0; sc < nCur; ++sc)
        {
            double best = std::numeric_limits<double>::infinity();
            std::size_t bestPrev = 0;
            for (std::size_t sp = 0; sp < nPrev; ++sp)
            {
                const double v = cost[sp] + transitionCost(frames[i - 1], sp, frames[i], sc);
                if (v < best)
                {
                    best = v;
                    bestPrev = sp;
                }
            }
            nextCost[sc] = best + observationCost(frames[i], sc);
            back[i][sc] = static_cast<std::uint8_t>(bestPrev);
        }
        cost = nextCost;
    }

    // 終端から遡って状態列を決める
    const std::size_t nLast = frames[numFrames - 1].count + 1;
    std::size_t state = 0;
    for (std::size_t s = 1; s < nLast; ++s)
    {
        if (cost[s] < cost[state])
        {
            state = s;
        }
    }
    std::vector<std::size_t> path(numFrames);
    path[numFrames - 1] = state;
    for (std::size_t i = numFrames - 1; i > 0; --i)
    {
        path[i - 1] = back[i][path[i]];
    }

    model::PitchTrack result;
    result.startSec = 0.0;
    result.hopSec = params.hopSec;
    result.f0Hz.assign(numFrames, 0.0f);
    result.voicing.assign(numFrames, 0.0f);
    for (std::size_t i = 0; i < numFrames; ++i)
    {
        if (path[i] >= frames[i].count)
        {
            continue; // 無声
        }
        const Candidate& cand = frames[i].items[path[i]];
        if (cand.dPrime > params.voicedThreshold)
        {
            continue;
        }
        result.f0Hz[i] = cand.f0Hz;
        result.voicing[i] = std::clamp(1.0f - cand.dPrime / kCmndThreshold, 0.0f, 1.0f);
    }
    return result;
}
} // namespace koenote::analysis
