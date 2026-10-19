// フェーズ2 のレンダリング: フォルマント（F-09）とゲイン・子音・ブレス（F-10）。

#include "render/GainEnvelope.h"
#include "render/PsolaRenderer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace koenote;
using Catch::Matchers::WithinRel;

namespace
{
constexpr double kSr = 44100.0;
constexpr double kF0 = 200.0;

// 0.2〜1.2 秒に、基本周波数 200 Hz・スペクトル包絡のピークが 1 kHz の声に近い音。
// 0〜0.2 秒の「子音」と 1.2〜1.6 秒の「ブレス」には広帯域のノイズを入れる。
struct Voice
{
    std::vector<float> audio;
    model::PitchTrack track;
    std::vector<model::Note> notes;

    Voice()
    {
        const auto n = static_cast<std::size_t>(1.8 * kSr);
        audio.assign(n, 0.0f);
        std::uint32_t seed = 1;
        const auto noise = [&seed] { seed = seed * 1664525u + 1013904223u; return static_cast<float>(seed >> 8) / 8388608.0f - 1.0f; };

        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double>(i) / kSr;

            if (t >= 0.2 && t < 1.2)
            {
                double v = 0.0;
                for (int h = 1; h <= 40; ++h)
                {
                    const double f = h * kF0;
                    const double envelope = std::exp(-0.5 * std::pow((f - 1000.0) / 500.0, 2.0));
                    v += envelope * std::sin(2.0 * std::numbers::pi * f * t);
                }
                audio[i] = static_cast<float>(0.15 * v);
            }
            else if (t < 0.2 || (t >= 1.2 && t < 1.6))
            {
                audio[i] = 0.1f * noise();
            }
        }

        track.hopSec = 0.005;
        const auto frames = static_cast<std::size_t>(1.8 / track.hopSec) + 1;
        track.f0Hz.assign(frames, 0.0f);
        track.voicing.assign(frames, 0.0f);

        for (std::size_t f = 0; f < frames; ++f)
        {
            const double t = track.timeOf(f);
            if (t >= 0.2 && t < 1.2)
            {
                track.f0Hz[f] = static_cast<float>(kF0);
                track.voicing[f] = 1.0f;
            }
        }

        model::Note a;
        a.id = 1;
        a.startSec = 0.2;
        a.endSec = 1.2;
        a.detectedMidi = static_cast<float>(69.0 + 12.0 * std::log2(kF0 / 440.0));
        notes = { a };
    }

    render::RenderedAudio render() const
    {
        const float* ptr = audio.data();
        render::RenderInput in;
        in.channels = &ptr;
        in.numChannels = 1;
        in.numFrames = static_cast<std::int64_t>(audio.size());
        in.sampleRate = kSr;
        in.track = &track;
        in.notes = &notes;
        return render::renderEditedAudio(in);
    }
};

double rms(const std::vector<float>& x, double from, double to)
{
    const auto i0 = static_cast<std::size_t>(from * kSr);
    const auto i1 = std::min(x.size(), static_cast<std::size_t>(to * kSr));
    double sum = 0.0;
    for (auto i = i0; i < i1; ++i)
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    return std::sqrt(sum / static_cast<double>(i1 - i0));
}

// 0.5〜0.8 秒付近の窓のスペクトル重心（Hz）。100 Hz〜6 kHz を 25 Hz 刻みの DFT で求める。
double spectralCentroid(const std::vector<float>& x)
{
    const std::size_t begin = static_cast<std::size_t>(0.5 * kSr);
    const std::size_t length = 4096;
    double weighted = 0.0, total = 0.0;

    for (double f = 100.0; f <= 6000.0; f += 25.0)
    {
        double re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < length; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(length - 1));
            const double ph = 2.0 * std::numbers::pi * f * static_cast<double>(i) / kSr;
            re += w * static_cast<double>(x[begin + i]) * std::cos(ph);
            im -= w * static_cast<double>(x[begin + i]) * std::sin(ph);
        }
        const double mag = std::sqrt(re * re + im * im);
        weighted += f * mag * mag;
        total += mag * mag;
    }

    return weighted / total;
}

// 自己相関のピークから基本周波数を推定する（倍音が多い波形でもゼロ交差より安定）
double estimateFrequency(const std::vector<float>& x, double from, double to)
{
    const auto i0 = static_cast<std::size_t>(from * kSr);
    const auto i1 = static_cast<std::size_t>(to * kSr);
    const auto minLag = static_cast<std::size_t>(kSr / 400.0);
    const auto maxLag = static_cast<std::size_t>(kSr / 100.0);

    double best = -1.0;
    std::size_t bestLag = 0;

    for (std::size_t lag = minLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        for (auto i = i0; i + lag < i1; ++i)
            sum += static_cast<double>(x[i]) * static_cast<double>(x[i + lag]);

        if (sum > best)
        {
            best = sum;
            bestLag = lag;
        }
    }

    return bestLag == 0 ? 0.0 : kSr / static_cast<double>(bestLag);
}
} // namespace

TEST_CASE("ゲインが全て 0 dB なら GainEnvelope は変更なし")
{
    Voice v;
    const render::GainEnvelope env(v.notes, 1.8);
    CHECK(env.isUnity());
    CHECK(env.gainAt(0.5) == 1.0f);
}

TEST_CASE("ノートのゲインは dB どおりの振幅変化になる")
{
    Voice v;
    v.notes[0].edit.gainDb = -6.0206f; // 約 1/2

    const auto original = v.audio;
    const auto out = v.render();
    REQUIRE(out.numChannels == 1);

    CHECK_THAT(rms(out.channels[0], 0.4, 1.0) / rms(original, 0.4, 1.0), WithinRel(0.5, 0.05));
    // ノート外（子音・ブレス）は変わらない
    CHECK_THAT(rms(out.channels[0], 0.0, 0.1) / rms(original, 0.0, 0.1), WithinRel(1.0, 0.1));
}

TEST_CASE("子音のゲインはノート直前だけ、ブレスのゲインはノート直後だけに掛かる")
{
    Voice v;
    v.notes[0].edit.consonantGainDb = -20.0f;
    v.notes[0].edit.breathGainDb = -20.0f;

    const auto original = v.audio;
    const auto out = v.render();
    REQUIRE(out.numChannels == 1);

    // 子音: 0.2 秒の直前 80 ms（0.12〜0.2）が小さくなり、それより前（0〜0.1）は変わらない
    CHECK(rms(out.channels[0], 0.13, 0.19) < 0.2 * rms(original, 0.13, 0.19));
    CHECK_THAT(rms(out.channels[0], 0.0, 0.1) / rms(original, 0.0, 0.1), WithinRel(1.0, 0.1));

    // ブレス: ノート直後（1.25〜1.55 秒）が小さくなる。ノート本体は変わらない。
    CHECK(rms(out.channels[0], 1.25, 1.55) < 0.2 * rms(original, 1.25, 1.55));
    CHECK_THAT(rms(out.channels[0], 0.4, 1.0) / rms(original, 0.4, 1.0), WithinRel(1.0, 0.05));
}

TEST_CASE("フォルマントを上げるとスペクトル重心が上がり、ピッチは変わらない")
{
    Voice v;
    const auto baseline = v.render().numChannels == 1 ? v.audio : std::vector<float> {};
    const double baseCentroid = spectralCentroid(baseline);

    v.notes[0].edit.formantShiftCents = 600.0f;
    const auto up = v.render();
    REQUIRE(up.numChannels == 1);

    const double upCentroid = spectralCentroid(up.channels[0]);
    CHECK(upCentroid > 1.15 * baseCentroid);
    CHECK(upCentroid < 1.8 * baseCentroid);
    CHECK_THAT(estimateFrequency(up.channels[0], 0.4, 1.0), WithinRel(kF0, 0.02));
}

TEST_CASE("フォルマントを下げるとスペクトル重心が下がる")
{
    Voice v;
    const double baseCentroid = spectralCentroid(v.audio);

    v.notes[0].edit.formantShiftCents = -600.0f;
    const auto down = v.render();
    REQUIRE(down.numChannels == 1);

    const double downCentroid = spectralCentroid(down.channels[0]);
    CHECK(downCentroid < 0.92 * baseCentroid);
    CHECK(downCentroid > 0.5 * baseCentroid);
    CHECK_THAT(estimateFrequency(down.channels[0], 0.4, 1.0), WithinRel(kF0, 0.02));
}

TEST_CASE("ピッチを上げても、フォルマントを追従させなければ声質（スペクトル重心）は大きく変わらない")
{
    Voice v;
    const double baseCentroid = spectralCentroid(v.audio);

    v.notes[0].edit.pitchOffsetCents = 600.0f;
    v.notes[0].edit.formantFollow = 0.0f;
    const auto preserved = v.render();
    REQUIRE(preserved.numChannels == 1);
    CHECK(spectralCentroid(preserved.channels[0]) < 1.25 * baseCentroid);

    v.notes[0].edit.formantFollow = 1.0f;
    const auto followed = v.render();
    REQUIRE(followed.numChannels == 1);
    // 追従させるとスペクトル重心も上がる
    CHECK(spectralCentroid(followed.channels[0]) > 1.2 * spectralCentroid(preserved.channels[0]));
}
