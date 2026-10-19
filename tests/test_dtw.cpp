#include "analysis/DtwAligner.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

using namespace koenote::analysis;

namespace
{
struct NoteSpec
{
    double startSec;
    double durSec;
    double f0Hz;
};

constexpr double kPitchesHz[] = { 220.0, 247.0, 277.0, 311.0, 349.0, 392.0 };

// 基準のノート列（6 ノート、ノート間は 0.2 s の無音）
std::vector<NoteSpec> makeBaseNotes()
{
    std::vector<NoteSpec> notes;
    for (int k = 0; k < 6; ++k)
    {
        notes.push_back({ 0.1 + 0.7 * k, 0.5, kPitchesHz[k] });
    }
    return notes;
}

// 区分伸縮: boundarySec より後ろを factor 倍に伸ばし、shiftSec だけずらす。
// 時刻の伸縮をノート単位で直接作るため、リサンプルは行わない。
std::vector<NoteSpec> warpNotes(const std::vector<NoteSpec>& notes, double boundarySec, double factor, double shiftSec)
{
    const auto stretch = [&](double t) { return t <= boundarySec ? t : boundarySec + (t - boundarySec) * factor; };
    std::vector<NoteSpec> out;
    for (const auto& n : notes)
    {
        const double s = shiftSec + stretch(n.startSec);
        const double e = shiftSec + stretch(n.startSec + n.durSec);
        out.push_back({ s, e - s, n.f0Hz });
    }
    return out;
}

// 基本波と 4 倍音まで（振幅 1/k）のノート列を描画する。ノート間は無音。
std::vector<float> renderNotes(double sr, const std::vector<NoteSpec>& notes, double totalSec)
{
    std::vector<float> x(static_cast<std::size_t>(std::llround(sr * totalSec)), 0.0f);
    for (const auto& note : notes)
    {
        const auto i0 = static_cast<std::size_t>(std::llround(note.startSec * sr));
        const auto n = static_cast<std::size_t>(std::llround(note.durSec * sr));
        for (std::size_t i = 0; i < n && i0 + i < x.size(); ++i)
        {
            const double t = static_cast<double>(i) / sr;
            const double env = std::min({ 1.0, t / 0.01, (note.durSec - t) / 0.01 }); // 10 ms の包絡
            double s = 0.0;
            for (int k = 1; k <= 5; ++k)
            {
                s += std::sin(2.0 * std::numbers::pi * k * note.f0Hz * t) / k;
            }
            x[i0 + i] = static_cast<float>(0.3 * env * s);
        }
    }
    return x;
}

AlignmentResult alignSignals(const std::vector<float>& g, double gsr, const std::vector<float>& t, double tsr,
                             const ProgressCallback& progress = {})
{
    return alignAudio(MonoAudioView{ g.data(), g.size(), gsr }, MonoAudioView{ t.data(), t.size(), tsr },
                      AlignmentOptions{}, progress);
}

constexpr double kSampleRates[] = { 16000.0, 22050.0, 44100.0 };
} // namespace

TEST_CASE("時間伸縮された声に近い信号で、各ノート開始時刻が 40 ms 以内に対応付く")
{
    const auto guideNotes = makeBaseNotes();
    // 境界 2.0 s より後ろを 1.25 倍に伸ばす
    const auto targetNotes = warpNotes(guideNotes, 2.0, 1.25, 0.0);

    for (const double sr : kSampleRates)
    {
        INFO("sampleRate = " << sr);
        const auto guide = renderNotes(sr, guideNotes, 4.6);
        const auto target = renderNotes(sr, targetNotes, 5.3);
        const auto r = alignSignals(guide, sr, target, sr);
        REQUIRE(r.valid);

        for (std::size_t k = 0; k < guideNotes.size(); ++k)
        {
            INFO("note = " << k);
            CHECK(std::abs(r.mapTargetToGuide(targetNotes[k].startSec) - guideNotes[k].startSec) <= 0.04);
        }
    }
}

TEST_CASE("同一信号同士の整列は対角線に沿う")
{
    for (const double sr : kSampleRates)
    {
        INFO("sampleRate = " << sr);
        const auto x = renderNotes(sr, makeBaseNotes(), 4.6);
        const auto r = alignSignals(x, sr, x, sr);
        REQUIRE(r.valid);

        for (const auto& a : r.path)
        {
            CHECK(std::abs(a.guideSec - a.targetSec) <= 0.02);
        }
    }
}

TEST_CASE("先頭に 0.5 秒の無音を足したターゲットは 0.5 秒ずれて対応付く")
{
    const auto guideNotes = makeBaseNotes();
    const auto targetNotes = warpNotes(guideNotes, 0.0, 1.0, 0.5);

    for (const double sr : kSampleRates)
    {
        INFO("sampleRate = " << sr);
        const auto guide = renderNotes(sr, guideNotes, 4.6);
        const auto target = renderNotes(sr, targetNotes, 5.1);
        const auto r = alignSignals(guide, sr, target, sr);
        REQUIRE(r.valid);

        for (std::size_t k = 0; k < guideNotes.size(); ++k)
        {
            INFO("note = " << k);
            // 定常音の内部は特徴量がほぼ一定のため、内部の時刻は特徴量から決まらない。ノート開始時刻のみを検査する
            CHECK(std::abs(r.mapTargetToGuide(targetNotes[k].startSec) - guideNotes[k].startSec) <= 0.04);
        }
    }
}

TEST_CASE("経路は単調非減少で、2 つの写像はノート開始時刻で逆写像になる")
{
    const auto guideNotes = makeBaseNotes();
    const auto targetNotes = warpNotes(guideNotes, 2.0, 1.25, 0.0);
    const double sr = 16000.0;
    const auto guide = renderNotes(sr, guideNotes, 4.6);
    const auto target = renderNotes(sr, targetNotes, 5.3);
    const auto r = alignSignals(guide, sr, target, sr);
    REQUIRE(r.valid);

    for (std::size_t i = 1; i < r.path.size(); ++i)
    {
        CHECK(r.path[i].guideSec >= r.path[i - 1].guideSec);
        CHECK(r.path[i].targetSec >= r.path[i - 1].targetSec);
    }
    for (std::size_t k = 0; k < targetNotes.size(); ++k)
    {
        const double onset = targetNotes[k].startSec;
        CHECK(std::abs(r.mapGuideToTarget(r.mapTargetToGuide(onset)) - onset) <= 0.02);
        CHECK(std::abs(r.mapTargetToGuide(r.mapGuideToTarget(guideNotes[k].startSec)) - guideNotes[k].startSec) <= 0.02);
    }
}

TEST_CASE("progress が false を返すと中断して valid=false になる")
{
    const auto x = renderNotes(16000.0, makeBaseNotes(), 4.6);
    const auto r = alignSignals(x, 16000.0, x, 16000.0, [](float) { return false; });
    CHECK_FALSE(r.valid);
    CHECK(r.path.empty());
}

TEST_CASE("入力が空・サンプルレート不正・極端に短い場合は valid=false になる")
{
    const auto x = renderNotes(16000.0, makeBaseNotes(), 4.6);
    const std::vector<float> shortSignal(1600, 0.1f); // 0.1 s（フレーム数 10 未満）

    CHECK_FALSE(alignAudio(MonoAudioView{ nullptr, 0, 16000.0 }, MonoAudioView{ x.data(), x.size(), 16000.0 }).valid);
    CHECK_FALSE(alignAudio(MonoAudioView{ x.data(), 0, 16000.0 }, MonoAudioView{ x.data(), x.size(), 16000.0 }).valid);
    CHECK_FALSE(alignAudio(MonoAudioView{ x.data(), x.size(), 0.0 }, MonoAudioView{ x.data(), x.size(), 16000.0 }).valid);
    CHECK_FALSE(alignSignals(shortSignal, 16000.0, x, 16000.0).valid);
}

TEST_CASE("3 分の信号同士の整列は 15 秒未満で完了する")
{
    const double sr = 44100.0;
    const double totalSec = 180.0;
    std::vector<NoteSpec> notes;
    for (int k = 0;; ++k)
    {
        const NoteSpec n{ 0.1 + 0.7 * k, 0.5, kPitchesHz[k % 6] };
        if (n.startSec + n.durSec > totalSec - 2.0)
        {
            break;
        }
        notes.push_back(n);
    }
    const auto x = renderNotes(sr, notes, totalSec);

    const auto begin = std::chrono::steady_clock::now();
    const auto r = alignSignals(x, sr, x, sr);
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();

    REQUIRE(r.valid);
    CHECK(elapsed < 15.0);
}
