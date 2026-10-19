#include "render/PsolaRenderer.h"

#include "render/PitchCurveEditor.h"
#include "render/PitchMarks.h"
#include "render/TimeMap.h"

#include <algorithm>
#include <cmath>

namespace koenote::render
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

bool isDefault(const model::NoteEdit& e) noexcept
{
    return e.pitchOffsetCents == 0.0f && e.centerCorrection == 0.0f && e.vibratoScale == 1.0f && e.driftScale == 1.0f
           && ! e.hasTimingEdit();
}

float adjustAt(const model::PitchTrack& track, const std::vector<float>& adjust, double srcSec) noexcept
{
    if (adjust.empty())
        return 0.0f;

    const auto frame = std::llround((srcSec - track.startSec) / track.hopSec);

    if (frame < 0 || static_cast<std::size_t>(frame) >= adjust.size())
        return 0.0f;

    return adjust[static_cast<std::size_t>(frame)];
}
} // namespace

bool hasAudibleEdits(const std::vector<model::Note>& notes) noexcept
{
    return std::any_of(notes.begin(), notes.end(), [] (const model::Note& n) { return ! isDefault(n.edit); });
}

RenderedAudio renderEditedAudio(const RenderInput& input, const RenderProgress& progress)
{
    RenderedAudio result;

    if (input.channels == nullptr || input.numChannels <= 0 || input.numFrames <= 0 || input.sampleRate <= 0.0)
        return result;

    const double sr = input.sampleRate;
    const auto numSrc = static_cast<std::size_t>(input.numFrames);
    const std::vector<model::Note> noNotes;
    const model::PitchTrack noTrack;
    const auto& notes = input.notes != nullptr ? *input.notes : noNotes;
    const auto& track = input.track != nullptr ? *input.track : noTrack;

    // 編集がなければ元音声をそのまま返す
    if (! hasAudibleEdits(notes))
    {
        result.numChannels = input.numChannels;
        result.sampleRate = sr;
        result.channels.resize(static_cast<std::size_t>(input.numChannels));

        for (int c = 0; c < input.numChannels; ++c)
            result.channels[static_cast<std::size_t>(c)].assign(input.channels[c], input.channels[c] + numSrc);

        return result;
    }

    // ピッチマークはモノラル化した音声から求める
    std::vector<float> mono(numSrc, 0.0f);
    const float monoGain = 1.0f / static_cast<float>(input.numChannels);

    for (int c = 0; c < input.numChannels; ++c)
        for (std::size_t i = 0; i < numSrc; ++i)
            mono[i] += input.channels[c][i] * monoGain;

    const auto marks = findPitchMarks(mono.data(), numSrc, sr, track);
    if (marks.empty())
        return result;

    const auto timeMap = TimeMap::fromNotes(notes);
    const auto adjust = computePitchAdjustCents(track, notes);

    const double srcDurationSec = static_cast<double>(numSrc) / sr;
    const double outDurationSec = std::max(timeMap.toOutput(srcDurationSec), 0.0);
    const auto maxOut = numSrc * 2 + static_cast<std::size_t>(30.0 * sr);
    const auto numOut = std::min(static_cast<std::size_t>(std::ceil(outDurationSec * sr)) + 1, maxOut);

    result.numChannels = input.numChannels;
    result.sampleRate = sr;
    result.channels.assign(static_cast<std::size_t>(input.numChannels), std::vector<float>(numOut, 0.0f));

    const auto maxHalf = static_cast<std::int64_t>(0.03 * sr);

    const auto markPeriod = [&] (std::size_t j, bool forward)
    {
        std::int64_t d = 0;

        if (forward)
            d = j + 1 < marks.size() ? marks[j + 1].position - marks[j].position
                                     : (j > 0 ? marks[j].position - marks[j - 1].position : 1);
        else
            d = j > 0 ? marks[j].position - marks[j - 1].position
                      : (marks.size() > 1 ? marks[1].position - marks[0].position : 1);

        return std::clamp<std::int64_t>(d, 1, maxHalf);
    };

    const double unvoicedStepOut = 0.005;
    const double outEnd = std::min(outDurationSec, static_cast<double>(numOut - 1) / sr);
    double t = std::max(0.0, timeMap.toOutput(static_cast<double>(marks.front().position) / sr));
    std::size_t hint = 0; // 直前に使ったマークの添字（探索の起点）
    std::size_t iteration = 0;

    while (t < outEnd)
    {
        if ((++iteration & 0x7FFu) == 0 && progress && ! progress(static_cast<float>(std::min(1.0, t / outEnd))))
            return RenderedAudio {};

        const double slope = timeMap.slopeAt(t);

        // 元音声を使わない区間（傾き 0）は無音のまま進める
        if (slope < 0.02)
        {
            t += unvoicedStepOut;
            continue;
        }

        const double srcPos = timeMap.toSource(t) * sr;

        // srcPos に最も近いマーク（hint から前後に進める。単調に動くことが多いため高速）
        std::size_t j = std::min(hint, marks.size() - 1);
        const auto pos = static_cast<std::int64_t>(std::llround(srcPos));

        while (j + 1 < marks.size() && std::llabs(marks[j + 1].position - pos) <= std::llabs(marks[j].position - pos))
            ++j;
        while (j > 0 && std::llabs(marks[j - 1].position - pos) < std::llabs(marks[j].position - pos))
            --j;
        hint = j;

        const auto& mark = marks[j];
        const auto periodFwd = markPeriod(j, true);
        const auto periodBack = markPeriod(j, false);

        double ratio = 1.0;
        if (mark.voiced)
            ratio = std::clamp(std::pow(2.0, static_cast<double>(adjustAt(track, adjust, srcPos / sr)) / 1200.0), 0.25, 4.0);

        // 出力側の周期（サンプル）。ratio = 1 のとき元の周期と等しい。
        const double periodOut = static_cast<double>(periodFwd) / ratio;
        // 窓の重みの和を 1 に保つ補正。ピッチを上げると窓が密に重なるため小さくする（identity では 1）。
        const double gain = periodOut / static_cast<double>(periodFwd);

        const auto outCentre = static_cast<std::int64_t>(std::llround(t * sr));

        for (int c = 0; c < input.numChannels; ++c)
        {
            const float* in = input.channels[c];
            auto& out = result.channels[static_cast<std::size_t>(c)];

            // 左半分（マークの手前 periodBack サンプル）と右半分（periodFwd サンプル）の Hann 窓
            for (std::int64_t k = -periodBack; k < periodFwd; ++k)
            {
                const auto src = mark.position + k;
                const auto dst = outCentre + k;

                if (src < 0 || src >= input.numFrames || dst < 0 || dst >= static_cast<std::int64_t>(numOut))
                    continue;

                const double span = static_cast<double>(k < 0 ? periodBack : periodFwd);
                const double w = 0.5 * (1.0 + std::cos(kPi * static_cast<double>(k) / span));
                out[static_cast<std::size_t>(dst)] += static_cast<float>(gain * w * static_cast<double>(in[src]));
            }
        }

        t += std::max(1.0, periodOut) / sr;
    }

    return result;
}
} // namespace koenote::render
