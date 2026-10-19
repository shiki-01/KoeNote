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

    // 2. 短い無声の連続を線形補間で橋渡しする（両側が有声のものだけ）
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

    for (const Span& region : regions)
    {
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
                    pieces.push_back({ noteFirst, devFirst - 1 });
                    noteFirst = devFirst;
                    sorted.clear();
                    for (std::size_t k = devFirst; k <= j; ++k)
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
