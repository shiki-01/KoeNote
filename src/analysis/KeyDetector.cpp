#include "analysis/KeyDetector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace koenote::analysis
{
namespace
{
constexpr std::size_t kPitchClasses = 12;
constexpr std::size_t kRankingSize = 5;

// Krumhansl-Kessler のプロファイル（主音 = 添字 0）
constexpr std::array<double, kPitchClasses> kMajorProfile = {
    6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr std::array<double, kPitchClasses> kMinorProfile = {
    6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

// confidence のスケール係数。1 位と 2 位の相関の差がこの値以上なら confidence = 1
// （Krumhansl-Schmuckler の典型的な候補間差から経験的に決めた値。テストで挙動を固定する）
constexpr double kConfidenceFullGap = 0.2;
constexpr double kEpsilon = 1e-12;

using Histogram = std::array<double, kPitchClasses>;

// ピッチクラスごとに durationSec で重み付けしたヒストグラム
Histogram buildHistogram(const std::vector<NoteForKey>& notes)
{
    Histogram hist{};
    for (const auto& n : notes)
    {
        const double midi = static_cast<double>(n.midi);
        if (! std::isfinite(midi) || ! std::isfinite(n.durationSec) || n.durationSec <= 0.0)
            continue;
        const auto rounded = static_cast<long long>(std::llround(midi));
        const auto pc = static_cast<std::size_t>(((rounded % 12) + 12) % 12);
        hist[pc] += n.durationSec;
    }
    return hist;
}

// 2 つのベクトルのピアソン相関係数。分母が 0 に近い場合は 0 を返す
double pearson(const Histogram& x, const Histogram& y)
{
    double meanX = 0.0;
    double meanY = 0.0;
    for (std::size_t i = 0; i < kPitchClasses; ++i)
    {
        meanX += x[i];
        meanY += y[i];
    }
    meanX /= static_cast<double>(kPitchClasses);
    meanY /= static_cast<double>(kPitchClasses);

    double cov = 0.0;
    double varX = 0.0;
    double varY = 0.0;
    for (std::size_t i = 0; i < kPitchClasses; ++i)
    {
        const double dx = x[i] - meanX;
        const double dy = y[i] - meanY;
        cov += dx * dy;
        varX += dx * dx;
        varY += dy * dy;
    }

    const double denom = std::sqrt(varX * varY);
    if (denom < kEpsilon)
        return 0.0;
    return cov / denom;
}

// 主音 root のキーに対応するプロファイルを作る（pc は主音からの音程で参照する）
Histogram rotatedProfile(const Histogram& profile, int root)
{
    Histogram rotated{};
    for (std::size_t pc = 0; pc < kPitchClasses; ++pc)
    {
        const std::size_t degree = (pc + kPitchClasses - static_cast<std::size_t>(root)) % kPitchClasses;
        rotated[pc] = profile[degree];
    }
    return rotated;
}
} // namespace

KeyResult detectKey(const std::vector<NoteForKey>& notes)
{
    KeyResult result;

    const Histogram hist = buildHistogram(notes);

    // 退化した入力（ノートなし、1 音、分散 0）はキーを決められないため無効とする。
    // ヒストグラムの分散 0 は全ピッチクラスの重みが等しい場合。1 音だけの場合は
    // ヒストグラムが 1 ピッチクラスに集中するため、非ゼロのピッチクラス数で判定する。
    std::size_t activeClasses = 0;
    double mean = 0.0;
    for (double v : hist)
    {
        if (v > 0.0)
            ++activeClasses;
        mean += v;
    }
    mean /= static_cast<double>(kPitchClasses);
    double variance = 0.0;
    for (double v : hist)
        variance += (v - mean) * (v - mean);
    variance /= static_cast<double>(kPitchClasses);

    if (activeClasses < 2 || variance < kEpsilon)
        return result;

    std::vector<KeyCandidate> candidates;
    candidates.reserve(kPitchClasses * 2);
    for (int root = 0; root < static_cast<int>(kPitchClasses); ++root)
    {
        const double majorScore = pearson(hist, rotatedProfile(kMajorProfile, root));
        candidates.push_back({ root, false, static_cast<float>(majorScore) });
        const double minorScore = pearson(hist, rotatedProfile(kMinorProfile, root));
        candidates.push_back({ root, true, static_cast<float>(minorScore) });
    }

    // score 降順。完全な同点の場合はメジャーを優先する。
    // 相対調（C メジャーと A マイナー）は同じ音集合を共有するため相関が近くなりやすい。
    // 近接値での優先は行わず、ranking の順序を単純な score 降順に保つ。
    std::stable_sort(candidates.begin(), candidates.end(), [](const KeyCandidate& a, const KeyCandidate& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return ! a.minor && b.minor;
    });

    result.valid = true;
    result.root = candidates[0].root;
    result.minor = candidates[0].minor;

    const double gap = static_cast<double>(candidates[0].score) - static_cast<double>(candidates[1].score);
    result.confidence = static_cast<float>(std::clamp(gap / kConfidenceFullGap, 0.0, 1.0));

    result.ranking.assign(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(kRankingSize));
    return result;
}
} // namespace koenote::analysis
