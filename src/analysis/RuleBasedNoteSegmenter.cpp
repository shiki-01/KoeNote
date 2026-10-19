#include "analysis/RuleBasedNoteSegmenter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace koenote::analysis
{
namespace
{
constexpr double kBridgeMaxSec = 0.040;      // この長さ以下の無声の連続は有声区間として橋渡しする
constexpr double kSmoothingHalfSec = 0.050;  // 移動 median の窓（中心から ±50 ms、約 100 ms）
constexpr float kSplitDeviationSemitone = 0.7f;
constexpr double kSplitMinSec = 0.050;       // 中心から外れた状態がこれ以上続いたら分割する
constexpr double kShortNoteSec = 0.060;      // これ未満のノートは隣へ併合する
constexpr double kEpsSec = 1e-9;
constexpr double kBridgeDipDb = 8.0;         // 無声の隙間の音量がこれ以上（dB）落ち込む場合は、橋渡しせず別のノートにする
constexpr double kSplitDipDb = 9.0;          // 有声区間内で、音量がこれ以上（dB）落ち込む谷は、ノートの境界にする
constexpr double kDipWindowSec = 0.080;      // 谷の前後の山を探す範囲
constexpr double kMinDipSpacingSec = 0.060;  // 谷同士の最小の間隔
constexpr float kBoundaryDeviationSemitone = 0.4f; // 音高の変化の開始位置を探すときの、元の中心からの偏差
constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

// フレーム番号の閉区間 [first, last]
struct Span
{
    std::size_t first = 0;
    std::size_t last = 0;
};

float toMidi(float hz)
{
    return 69.0f + 12.0f * std::log2(hz / 440.0f);
}

// 空でない vector の median（引数は値渡しで並べ替える）
float medianOf(std::vector<float> v)
{
    const std::size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
    if (v.size() % 2 == 1)
    {
        return v[mid];
    }
    const float lower = *std::max_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid));
    return 0.5f * (lower + v[mid]);
}

// 昇順ソート済みの vector の median
float medianSorted(const std::vector<float>& s)
{
    const std::size_t n = s.size();
    if (n % 2 == 1)
    {
        return s[n / 2];
    }
    return 0.5f * (s[n / 2 - 1] + s[n / 2]);
}

void insertSorted(std::vector<float>& s, float value)
{
    s.insert(std::upper_bound(s.begin(), s.end(), value), value);
}

float spanMedian(const std::vector<float>& values, Span sp)
{
    const auto b = values.begin() + static_cast<std::ptrdiff_t>(sp.first);
    const auto e = values.begin() + static_cast<std::ptrdiff_t>(sp.last + 1);
    return medianOf(std::vector<float>(b, e));
}

double spanSec(Span sp, double hop)
{
    return static_cast<double>(sp.last - sp.first + 1) * hop;
}

bool isShort(Span sp, double hop)
{
    return spanSec(sp, hop) < kShortNoteSec - kEpsSec;
}

// 区間 [startSec, endSec] の RMS（0..1 にクリップ）。音声がなければ 0
float rmsLevel(const MonoAudioView& audio, double startSec, double endSec)
{
    if (audio.samples == nullptr || audio.numSamples == 0 || !(audio.sampleRate > 0.0))
    {
        return 0.0f;
    }
    const double n = static_cast<double>(audio.numSamples);
    const std::size_t i0 = static_cast<std::size_t>(std::min(n, std::floor(std::max(0.0, startSec) * audio.sampleRate)));
    const std::size_t i1 = static_cast<std::size_t>(std::min(n, std::ceil(std::max(0.0, endSec) * audio.sampleRate)));
    if (i1 <= i0)
    {
        return 0.0f;
    }
    double ss = 0.0;
    for (std::size_t i = i0; i < i1; ++i)
    {
        const double v = static_cast<double>(audio.samples[i]);
        ss += v * v;
    }
    const double rms = std::sqrt(ss / static_cast<double>(i1 - i0));
    return std::clamp(static_cast<float>(rms), 0.0f, 1.0f);
}

// フレームごとの音量（dB）。フレーム時刻の前後 1 hop の RMS。音声がなければ空。
std::vector<float> frameEnergyDb(const model::PitchTrack& track, const MonoAudioView& audio)
{
    std::vector<float> db;
    if (audio.samples == nullptr || audio.numSamples == 0 || !(audio.sampleRate > 0.0))
    {
        return db;
    }
    db.resize(track.size());
    for (std::size_t i = 0; i < track.size(); ++i)
    {
        const double t = track.timeOf(i);
        const float rms = rmsLevel(audio, t - track.hopSec, t + track.hopSec);
        db[i] = 20.0f * std::log10(std::max(rms, 1.0e-6f));
    }
    return db;
}

// 有声区間を、音量の深い谷で分ける（同じ音高が続くノートの境界は、音高ではなく音量の谷に現れるため）。
// 谷の底のフレームを中心に、周囲より kSplitDipDb 以上低い連続するフレームは、どのノートにも含めない。
std::vector<Span> splitAtEnergyDips(Span region, const std::vector<float>& energy, double hop)
{
    if (energy.empty())
    {
        return { region };
    }

    // ±2 フレーム（約 ±10 ms）の移動平均で、ピッチ周期やノイズによる細かい揺れを均す
    std::vector<float> smooth(region.last - region.first + 1);
    for (std::size_t k = region.first; k <= region.last; ++k)
    {
        const std::size_t lo = (k - region.first >= 2) ? k - 2 : region.first;
        const std::size_t hi = std::min(region.last, k + 2);
        float sum = 0.0f;
        for (std::size_t j = lo; j <= hi; ++j)
        {
            sum += energy[j];
        }
        smooth[k - region.first] = sum / static_cast<float>(hi - lo + 1);
    }

    const std::size_t win = std::max<std::size_t>(2, static_cast<std::size_t>(std::llround(kDipWindowSec / hop)));
    const std::size_t spacing = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(kMinDipSpacingSec / hop)));
    const std::size_t len = smooth.size();

    std::vector<Span> dips; // 谷（ノートに含めないフレームの閉区間、region 内の相対位置）
    std::size_t lastDipEnd = 0;
    bool haveDip = false;

    for (std::size_t k = 1; k + 1 < len; ++k)
    {
        if (!(smooth[k] <= smooth[k - 1] && smooth[k] <= smooth[k + 1]))
        {
            continue;
        }

        float leftPeak = smooth[k], rightPeak = smooth[k];
        for (std::size_t j = (k >= win ? k - win : 0); j <= k; ++j)
        {
            leftPeak = std::max(leftPeak, smooth[j]);
        }
        for (std::size_t j = k; j <= std::min(len - 1, k + win); ++j)
        {
            rightPeak = std::max(rightPeak, smooth[j]);
        }

        const float peak = std::min(leftPeak, rightPeak);
        if (peak - smooth[k] < static_cast<float>(kSplitDipDb))
        {
            continue;
        }

        // 谷の幅: 谷底から、「山 - 6 dB」を下回る範囲
        const float limit = peak - 6.0f;
        std::size_t b = k, e = k;
        while (b > 0 && smooth[b - 1] < limit)
        {
            --b;
        }
        while (e + 1 < len && smooth[e + 1] < limit)
        {
            ++e;
        }

        if (haveDip && b < lastDipEnd + spacing)
        {
            continue;
        }

        dips.push_back({ b, e });
        lastDipEnd = e;
        haveDip = true;
    }

    std::vector<Span> parts;
    std::size_t begin = 0;
    for (const Span& d : dips)
    {
        if (d.first > begin)
        {
            parts.push_back({ region.first + begin, region.first + d.first - 1 });
        }
        begin = d.last + 1;
    }
    if (begin < len)
    {
        parts.push_back({ region.first + begin, region.last });
    }
    return parts;
}
} // namespace

std::vector<model::Note> RuleBasedNoteSegmenter::segment(const model::PitchTrack& track, const MonoAudioView& audio)
{
    std::vector<model::Note> notes;
    const std::size_t n = track.size();
    const double hop = track.hopSec;
    if (n == 0 || !(hop > 0.0))
    {
        return notes;
    }

    // 1. MIDI 値への変換
    std::vector<float> midi(n, 0.0f);
    std::vector<bool> voiced(n, false);
    for (std::size_t i = 0; i < n; ++i)
    {
        if (track.f0Hz[i] > 0.0f)
        {
            midi[i] = toMidi(track.f0Hz[i]);
            voiced[i] = true;
        }
    }

    // フレームごとの音量（dB）。同じ音高のノートの切れ目を、音高の変化ではなく音量の谷から見つけるために使う
    const std::vector<float> energy = frameEnergyDb(track, audio);

    // 2. 短い無声の連続を線形補間で橋渡しする（両側が有声のものだけ。音量が深く落ち込む隙間は橋渡ししない）
    for (std::size_t i = 0; i < n;)
    {
        if (voiced[i])
        {
            ++i;
            continue;
        }
        const std::size_t gapBegin = i;
        while (i < n && !voiced[i])
        {
            ++i;
        }
        const std::size_t gapEnd = i; // 排他
        const bool bridgeable = gapBegin > 0 && gapEnd < n
            && static_cast<double>(gapEnd - gapBegin) * hop <= kBridgeMaxSec + kEpsSec;
        if (!bridgeable)
        {
            continue;
        }
        if (!energy.empty())
        {
            const std::size_t side = std::min<std::size_t>(4, gapBegin);
            const std::size_t sideRight = std::min<std::size_t>(4, n - gapEnd);
            float before = -200.0f, after = -200.0f, lowest = 200.0f;
            for (std::size_t k = gapBegin - side; k < gapBegin; ++k)
            {
                before = std::max(before, energy[k]);
            }
            for (std::size_t k = gapEnd; k < gapEnd + sideRight; ++k)
            {
                after = std::max(after, energy[k]);
            }
            for (std::size_t k = gapBegin; k < gapEnd; ++k)
            {
                lowest = std::min(lowest, energy[k]);
            }
            if (std::min(before, after) - lowest >= static_cast<float>(kBridgeDipDb))
            {
                continue;
            }
        }
        const float left = midi[gapBegin - 1];
        const float right = midi[gapEnd];
        const float len = static_cast<float>(gapEnd - gapBegin + 1);
        for (std::size_t k = gapBegin; k < gapEnd; ++k)
        {
            const float t = static_cast<float>(k - gapBegin + 1) / len;
            midi[k] = left + (right - left) * t;
            voiced[k] = true;
        }
    }

    // 有声区間（閉区間）
    std::vector<Span> regions;
    for (std::size_t i = 0; i < n;)
    {
        if (!voiced[i])
        {
            ++i;
            continue;
        }
        const std::size_t begin = i;
        while (i < n && voiced[i])
        {
            ++i;
        }
        regions.push_back({ begin, i - 1 });
    }

    const std::size_t halfFrames = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::llround(kSmoothingHalfSec / hop)));

    std::vector<Span> spans; // 時間順・重なりなし
    std::vector<float> smoothed(n, 0.0f);

    // 音量の深い谷で有声区間を分ける
    std::vector<Span> parts;
    for (const Span& region : regions)
    {
        for (const Span& part : splitAtEnergyDips(region, energy, hop))
        {
            parts.push_back(part);
        }
    }

    // 平滑化の弱い版（±2 フレームの median）。音高の変化の開始位置を探すのに使う。
    std::vector<float> light(n, 0.0f);

    for (const Span& region : parts)
    {
        for (std::size_t k = region.first; k <= region.last; ++k)
        {
            const std::size_t lo = (k - region.first >= 2) ? k - 2 : region.first;
            const std::size_t hi = std::min(region.last, k + 2);
            light[k] = spanMedian(midi, { lo, hi });
        }

        // 3. 移動 median による平滑化
        for (std::size_t k = region.first; k <= region.last; ++k)
        {
            const std::size_t lo = (k - region.first >= halfFrames) ? k - halfFrames : region.first;
            const std::size_t hi = std::min(region.last, k + halfFrames);
            smoothed[k] = spanMedian(midi, { lo, hi });
        }

        // 4. ヒステリシス付きの分割
        std::vector<Span> pieces;
        std::size_t noteFirst = region.first;
        std::vector<float> sorted{ smoothed[region.first] };
        std::size_t devFirst = kNone; // 中心から外れた連続区間の先頭
        for (std::size_t j = region.first + 1; j <= region.last; ++j)
        {
            const float center = medianSorted(sorted);
            if (std::fabs(smoothed[j] - center) > kSplitDeviationSemitone)
            {
                if (devFirst == kNone)
                {
                    devFirst = j;
                }
                if (spanSec({ devFirst, j }, hop) >= kSplitMinSec - kEpsSec)
                {
                    // 平滑化（約 100 ms の median）のため、検出は音高の変化より遅れる。境界は、弱い平滑化の曲線が
                    // 元の音高から離れ始めた位置まで戻す（ポルタメントの開始位置 = ノートの開始位置）。
                    const float oldCenter = center;
                    std::size_t boundary = devFirst;
                    while (boundary > noteFirst + 2 && std::fabs(light[boundary - 1] - oldCenter) > kBoundaryDeviationSemitone)
                    {
                        --boundary;
                    }

                    pieces.push_back({ noteFirst, boundary - 1 });
                    noteFirst = boundary;
                    sorted.clear();
                    for (std::size_t k = boundary; k <= j; ++k)
                    {
                        insertSorted(sorted, smoothed[k]);
                    }
                    devFirst = kNone;
                }
            }
            else
            {
                devFirst = kNone;
                insertSorted(sorted, smoothed[j]);
            }
        }
        pieces.push_back({ noteFirst, region.last });

        // 5. 短いノートを隣へ併合する（隣がなければ破棄）
        while (pieces.size() > 1)
        {
            const auto it = std::find_if(pieces.begin(), pieces.end(), [hop](Span sp) { return isShort(sp, hop); });
            if (it == pieces.end())
            {
                break;
            }
            const std::size_t idx = static_cast<std::size_t>(it - pieces.begin());
            std::size_t nb = 0;
            if (idx == 0)
            {
                nb = 1;
            }
            else if (idx + 1 == pieces.size())
            {
                nb = idx - 1;
            }
            else
            {
                const float m = spanMedian(midi, *it);
                const float dl = std::fabs(m - spanMedian(midi, pieces[idx - 1]));
                const float dr = std::fabs(m - spanMedian(midi, pieces[idx + 1]));
                nb = (dl <= dr) ? idx - 1 : idx + 1;
            }
            const Span merged{ std::min(pieces[idx].first, pieces[nb].first),
                               std::max(pieces[idx].last, pieces[nb].last) };
            const std::size_t keep = std::min(idx, nb);
            const std::size_t drop = std::max(idx, nb);
            pieces[keep] = merged;
            pieces.erase(pieces.begin() + static_cast<std::ptrdiff_t>(drop));
        }
        if (pieces.size() == 1 && isShort(pieces[0], hop))
        {
            pieces.clear();
        }

        spans.insert(spans.end(), pieces.begin(), pieces.end());
    }

    // 6. ノートの生成。隣接するノート同士は境界の値を共有する
    const double half = 0.5 * hop;
    notes.reserve(spans.size());
    for (std::size_t k = 0; k < spans.size(); ++k)
    {
        const Span sp = spans[k];
        const bool adjacentToPrev = k > 0 && spans[k - 1].last + 1 == sp.first;

        model::Note note;
        note.startSec = adjacentToPrev ? notes[k - 1].endSec : std::max(0.0, track.timeOf(sp.first) - half);
        note.endSec = track.timeOf(sp.last) + half;
        note.detectedMidi = spanMedian(midi, sp);
        note.level = rmsLevel(audio, note.startSec, note.endSec);
        notes.push_back(note);
    }
    return notes;
}
} // namespace koenote::analysis
