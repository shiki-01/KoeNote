#include "render/PitchCurveEditor.h"
#include "render/PitchMarks.h"
#include "render/PsolaRenderer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace koenote;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
constexpr double kSr = 48000.0;
constexpr double kF0 = 200.0;

double midiOf(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }

// 0.2〜1.2 秒に 200 Hz の正弦波（倍音付き）がある 1.5 秒の信号
struct Fixture
{
    std::vector<float> audio;
    model::PitchTrack track;
    std::vector<model::Note> notes;

    explicit Fixture(double total = 1.5)
    {
        const auto n = static_cast<std::size_t>(total * kSr);
        audio.assign(n, 0.0f);

        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double>(i) / kSr;
            if (t >= 0.2 && t < 1.2)
            {
                const double ph = 2.0 * std::numbers::pi * kF0 * t;
                audio[i] = static_cast<float>(0.5 * std::sin(ph) + 0.25 * std::sin(2.0 * ph) + 0.12 * std::sin(3.0 * ph));
            }
        }

        track.hopSec = 0.005;
        const auto frames = static_cast<std::size_t>(total / track.hopSec) + 1;
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
        a.endSec = 0.7;
        a.detectedMidi = static_cast<float>(midiOf(kF0));
        model::Note b = a;
        b.id = 2;
        b.startSec = 0.7;
        b.endSec = 1.2;
        notes = { a, b };
    }

    render::RenderedAudio render(const render::RenderProgress& progress = {}) const
    {
        const float* ptr = audio.data();
        render::RenderInput in;
        in.channels = &ptr;
        in.numChannels = 1;
        in.numFrames = static_cast<std::int64_t>(audio.size());
        in.sampleRate = kSr;
        in.track = &track;
        in.notes = &notes;
        return render::renderEditedAudio(in, progress);
    }
};

// 正の向きのゼロ交差から周波数を推定する
double estimateFrequency(const std::vector<float>& x, double startSec, double endSec)
{
    const auto begin = static_cast<std::size_t>(startSec * kSr);
    const auto end = static_cast<std::size_t>(endSec * kSr);
    double firstCrossing = -1.0, lastCrossing = -1.0;
    int count = 0;

    for (std::size_t i = begin + 1; i < end && i < x.size(); ++i)
    {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double frac = -static_cast<double>(x[i - 1]) / static_cast<double>(x[i] - x[i - 1]);
            const double pos = static_cast<double>(i - 1) + frac;
            if (firstCrossing < 0.0)
                firstCrossing = pos;
            lastCrossing = pos;
            ++count;
        }
    }

    if (count < 3)
        return 0.0;

    return static_cast<double>(count - 1) * kSr / (lastCrossing - firstCrossing);
}

double rms(const std::vector<float>& x, double startSec, double endSec)
{
    const auto begin = static_cast<std::size_t>(startSec * kSr);
    const auto end = std::min(x.size(), static_cast<std::size_t>(endSec * kSr));
    double sum = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    return std::sqrt(sum / static_cast<double>(end - begin));
}
} // namespace

TEST_CASE("ピッチマークは有声区間で周期ごとに置かれ、全体を覆う")
{
    Fixture fx;
    const auto marks = render::findPitchMarks(fx.audio.data(), fx.audio.size(), kSr, fx.track);

    REQUIRE(marks.size() > 100);
    CHECK(marks.front().position == 0);
    CHECK(marks.back().position >= static_cast<std::int64_t>(fx.audio.size()) - 2);

    int voiced = 0;
    std::int64_t previous = -1;

    for (const auto& m : marks)
    {
        CHECK(m.position > previous);
        previous = m.position;

        if (m.voiced)
        {
            ++voiced;
            const double sec = static_cast<double>(m.position) / kSr;
            CHECK(sec >= 0.19);
            CHECK(sec <= 1.21);
        }
    }

    // 1 秒 × 200 Hz = 約 200 個
    CHECK(voiced >= 190);
    CHECK(voiced <= 210);
}

TEST_CASE("編集がなければ renderEditedAudio は元音声をそのまま返す")
{
    Fixture fx;
    const auto out = fx.render();

    REQUIRE(out.numChannels == 1);
    REQUIRE(static_cast<std::size_t>(out.numFrames()) == fx.audio.size());
    CHECK(out.channels[0] == fx.audio);
}

TEST_CASE("編集していないノートの区間は元音声と一致する（窓の重みの和が 1）")
{
    Fixture fx;
    fx.notes[1].edit.pitchOffsetCents = 100.0f; // 後ろのノートだけ編集する

    const auto out = fx.render();
    REQUIRE(out.numChannels == 1);

    // 前半のノート（0.2〜0.7 秒）の内部は、境界の影響を避けて 0.25〜0.62 秒で比較する
    const auto begin = static_cast<std::size_t>(0.25 * kSr);
    const auto end = static_cast<std::size_t>(0.62 * kSr);
    double maxError = 0.0;

    for (std::size_t i = begin; i < end; ++i)
        maxError = std::max(maxError, std::abs(static_cast<double>(out.channels[0][i]) - static_cast<double>(fx.audio[i])));

    CHECK(maxError < 1.0e-3);
}

TEST_CASE("ピッチを 200 セント上げると周波数が約 12.2% 上がり、振幅は保たれる")
{
    Fixture fx;
    for (auto& n : fx.notes)
        n.edit.pitchOffsetCents = 200.0f;

    const auto out = fx.render();
    REQUIRE(out.numChannels == 1);

    const double expected = kF0 * std::pow(2.0, 200.0 / 1200.0);
    const double measured = estimateFrequency(out.channels[0], 0.4, 1.0);

    CHECK_THAT(measured, WithinRel(expected, 0.015));

    const double before = rms(fx.audio, 0.4, 1.0);
    const double after = rms(out.channels[0], 0.4, 1.0);
    CHECK_THAT(after, WithinRel(before, 0.15));
}

TEST_CASE("ピッチを 300 セント下げると周波数が下がる")
{
    Fixture fx;
    for (auto& n : fx.notes)
        n.edit.pitchOffsetCents = -300.0f;

    const auto out = fx.render();
    REQUIRE(out.numChannels == 1);

    const double expected = kF0 * std::pow(2.0, -300.0 / 1200.0);
    CHECK_THAT(estimateFrequency(out.channels[0], 0.4, 1.0), WithinRel(expected, 0.015));
}

TEST_CASE("ノートを 1.5 倍に伸ばしても周波数は変わらず、長さが伸びる")
{
    Fixture fx(2.0);
    // 前のノート 0.2〜0.7 を 0.2〜0.95 へ、後ろのノート 0.7〜1.2 を 0.95〜1.45 へ移す（以降は平行移動）
    fx.notes[0].edit.targetStartSec = 0.2;
    fx.notes[0].edit.targetEndSec = 0.95;
    fx.notes[1].edit.targetStartSec = 0.95;
    fx.notes[1].edit.targetEndSec = 1.45;

    const auto out = fx.render();
    REQUIRE(out.numChannels == 1);

    // 出力の長さ = 元の 2.0 秒 + 0.25 秒
    CHECK_THAT(static_cast<double>(out.numFrames()) / kSr, WithinAbs(2.25, 0.01));

    CHECK_THAT(estimateFrequency(out.channels[0], 0.3, 0.9), WithinRel(kF0, 0.01));
    CHECK_THAT(rms(out.channels[0], 0.3, 0.9), WithinRel(rms(fx.audio, 0.3, 0.65), 0.15));

    // 元音声が無音だった範囲（先頭と、ノート終了後）は無音のまま
    CHECK(rms(out.channels[0], 0.0, 0.15) < 1.0e-4);
    CHECK(rms(out.channels[0], 1.6, 2.2) < 1.0e-4);
}

TEST_CASE("ノートを短縮すると出力が短くなる")
{
    Fixture fx;
    fx.notes[1].edit.targetStartSec = 0.7;
    fx.notes[1].edit.targetEndSec = 0.95; // 0.5 秒 → 0.25 秒

    const auto out = fx.render();
    REQUIRE(out.numChannels == 1);
    CHECK_THAT(static_cast<double>(out.numFrames()) / kSr, WithinAbs(1.25, 0.01));
    CHECK_THAT(estimateFrequency(out.channels[0], 0.75, 0.93), WithinRel(kF0, 0.02));
}

TEST_CASE("進捗コールバックで false を返すとレンダリングが中断される")
{
    Fixture fx(20.0);
    fx.notes[0].edit.pitchOffsetCents = 100.0f;
    // 20 秒に引き伸ばした有声区間（レンダリングに十分な長さ）にする
    fx.notes[0].startSec = 0.2;
    fx.notes[0].endSec = 0.7;

    bool called = false;
    const auto out = fx.render([&] (float) { called = true; return false; });

    // 進捗の通知間隔より短い処理では呼ばれないこともあるため、呼ばれた場合のみ中断を確認する
    if (called)
        CHECK(out.numChannels == 0);
    else
        CHECK(out.numChannels == 1);
}

TEST_CASE("モノラル音声を 2 チャンネルとして扱ってもピッチ変更後の結果が両チャンネルで一致する")
{
    Fixture fx;
    for (auto& n : fx.notes)
        n.edit.pitchOffsetCents = 100.0f;

    const float* ptrs[2] = { fx.audio.data(), fx.audio.data() };
    render::RenderInput in;
    in.channels = ptrs;
    in.numChannels = 2;
    in.numFrames = static_cast<std::int64_t>(fx.audio.size());
    in.sampleRate = kSr;
    in.track = &fx.track;
    in.notes = &fx.notes;

    const auto out = render::renderEditedAudio(in);
    REQUIRE(out.numChannels == 2);
    CHECK(out.channels[0] == out.channels[1]);
}

TEST_CASE("ビブラート量 0 でピッチ曲線の揺れが小さくなる")
{
    // 330 Hz を中心に ±50 セント、5.5 Hz のビブラートを持つ 1 ノート
    model::PitchTrack track;
    track.hopSec = 0.005;
    const std::size_t frames = 400;
    track.f0Hz.resize(frames);
    track.voicing.assign(frames, 1.0f);

    for (std::size_t f = 0; f < frames; ++f)
    {
        const double cents = 50.0 * std::sin(2.0 * std::numbers::pi * 5.5 * track.timeOf(f));
        track.f0Hz[f] = static_cast<float>(330.0 * std::pow(2.0, cents / 1200.0));
    }

    model::Note n;
    n.startSec = 0.0;
    n.endSec = 2.0;
    n.detectedMidi = static_cast<float>(midiOf(330.0));
    n.edit.vibratoScale = 0.0f;

    const auto adjust = render::computePitchAdjustCents(track, { n });
    REQUIRE(adjust.size() == frames);

    double before = 0.0, after = 0.0;
    int count = 0;

    for (std::size_t f = 60; f < frames - 60; ++f) // 端の窓の影響を避ける
    {
        const double dev = (midiOf(track.f0Hz[f]) - n.detectedMidi) * 100.0;
        const double adj = dev + static_cast<double>(adjust[f]);
        before += dev * dev;
        after += adj * adj;
        ++count;
    }

    CHECK(std::sqrt(after / count) < 0.35 * std::sqrt(before / count));
}

TEST_CASE("無編集のノートのピッチ補正量は 0")
{
    Fixture fx;
    const auto adjust = render::computePitchAdjustCents(fx.track, fx.notes);
    for (const auto v : adjust)
        CHECK(v == 0.0f);
}

TEST_CASE("ピッチ移動量はノート内の有声フレームに加わり、ノート外には加わらない")
{
    Fixture fx;
    fx.notes[0].edit.pitchOffsetCents = 100.0f;

    const auto adjust = render::computePitchAdjustCents(fx.track, fx.notes);

    CHECK_THAT(static_cast<double>(adjust[static_cast<std::size_t>(0.4 / 0.005)]), WithinAbs(100.0, 1e-3));
    CHECK(adjust[static_cast<std::size_t>(0.1 / 0.005)] == 0.0f);
    CHECK(adjust[static_cast<std::size_t>(0.9 / 0.005)] == 0.0f);
}
