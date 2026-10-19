// ピッチ推定・ノート分割の精度を、合成した歌声（正解の F0・ノートが既知）で測る。
// 指標は mir_eval と同じ定義（要件定義書 4章）:
//   RPA          : 正解が有声のフレームのうち、推定が有声で正解から ±50 セント以内の割合
//   オクターブ誤り: 正解が有声のフレームのうち、推定が整数オクターブ（±50 セント）ずれている割合
//   COnP の F 値 : ノートの開始時刻（±50 ms）と音高（±50 セント）が一致する割合
// 実際の歌声での精度は tools/eval で評価する（本人の歌声と正解 MIDI が必要）。ここは回帰テストを兼ねる。

#include "analysis/RuleBasedNoteSegmenter.h"
#include "analysis/YinPitchAnalyzer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <utility>
#include <vector>

using namespace koenote;

namespace
{
struct RefNote
{
    double start, end, hz;
};

struct Singer
{
    std::vector<float> audio;
    std::vector<RefNote> notes;
    std::vector<double> f0PerFrame; // hop 5 ms、無声は 0
    double sampleRate = 44100.0;
};

double cents(double hz, double ref) { return 1200.0 * std::log2(hz / ref); }

// 歌声に近い合成: 母音のフォルマント（共鳴）、ビブラート（遅れて立ち上がる）、ポルタメント、息のノイズ、
// ノート間の短い隙間。音域は男声〜女声（C3〜C5）。
Singer makeSinger(unsigned seed, double sr, int numNotes, bool noisy, double lowMidi = 48.0, double highMidi = 72.0)
{
    std::mt19937 rng(seed);
    const auto uniform = [&rng] (double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };

    Singer s;
    s.sampleRate = sr;

    // ノート列（半音で C3〜C5。直前から ±5 半音以内）
    double midi = uniform(lowMidi + 4.0, highMidi - 5.0);
    double t = 0.3;
    for (int i = 0; i < numNotes; ++i)
    {
        midi = std::clamp(midi + std::round(uniform(-5.0, 5.0)), lowMidi, highMidi);
        const double dur = uniform(0.3, 1.0);
        s.notes.push_back({ t, t + dur, 440.0 * std::pow(2.0, (midi - 69.0) / 12.0) });
        t += dur + uniform(0.03, 0.12);
    }

    const double total = t + 0.3;
    const auto n = static_cast<std::size_t>(total * sr);
    s.audio.assign(n, 0.0f);
    const std::size_t frames = static_cast<std::size_t>(total / 0.005) + 1;
    s.f0PerFrame.assign(frames, 0.0);

    // 各サンプルの瞬時 F0 を決め、位相を積分して倍音を合成する
    std::vector<double> f0(n, 0.0);
    std::vector<double> amp(n, 0.0);

    for (std::size_t k = 0; k < s.notes.size(); ++k)
    {
        const auto& note = s.notes[k];
        const double prevHz = k > 0 ? s.notes[k - 1].hz : note.hz;
        const double vibRate = uniform(5.0, 6.5);
        const double vibDepth = uniform(20.0, 60.0); // セント
        const double vibDelay = uniform(0.1, 0.25);
        const double portamento = 0.06; // 直前のノートから滑らかにつなぐ

        for (auto i = static_cast<std::size_t>(note.start * sr); i < std::min(n, static_cast<std::size_t>(note.end * sr)); ++i)
        {
            const double local = static_cast<double>(i) / sr - note.start;
            double centsOffset = vibDepth * std::sin(2.0 * std::numbers::pi * vibRate * local) * std::clamp((local - vibDelay) / 0.2, 0.0, 1.0);
            double base = note.hz;

            if (local < portamento && k > 0)
                base = prevHz * std::pow(note.hz / prevHz, 0.5 - 0.5 * std::cos(std::numbers::pi * local / portamento));

            f0[i] = base * std::pow(2.0, centsOffset / 1200.0);
            const double fadeIn = std::clamp(local / 0.015, 0.0, 1.0);
            const double fadeOut = std::clamp((note.end - note.start - local) / 0.03, 0.0, 1.0);
            amp[i] = 0.3 * fadeIn * fadeOut;
        }
    }

    // 母音ごとのフォルマント（ノートごとに変える）
    std::vector<std::array<double, 3>> vowels(s.notes.size());
    for (auto& v : vowels)
        v = { uniform(300.0, 800.0), uniform(900.0, 2200.0), uniform(2400.0, 3200.0) };

    double phase = 0.0;
    std::size_t noteIndex = 0;

    for (std::size_t i = 0; i < n; ++i)
    {
        if (f0[i] <= 0.0)
            continue;

        const double tSec = static_cast<double>(i) / sr;
        while (noteIndex + 1 < s.notes.size() && tSec >= s.notes[noteIndex].end)
            ++noteIndex;

        phase += 2.0 * std::numbers::pi * f0[i] / sr;
        const auto& vowel = vowels[noteIndex];
        double v = 0.0;

        for (int h = 1; h * f0[i] < 0.45 * sr && h <= 40; ++h)
        {
            const double f = h * f0[i];
            double env = 0.0;
            for (const double formant : vowel)
                env += std::exp(-0.5 * std::pow((f - formant) / 350.0, 2.0));
            env = env / (1.0 + 0.0006 * f) + 0.02;
            v += env * std::sin(phase * h);
        }

        s.audio[i] = static_cast<float>(amp[i] * v / 3.0);
    }

    // 息のノイズ（全体に薄く）。noisy ならさらに大きく、ホワイトノイズの背景も足す。
    std::normal_distribution<double> gauss(0.0, 1.0);
    const double noiseLevel = noisy ? 0.02 : 0.004;
    for (auto& x : s.audio)
        x += static_cast<float>(noiseLevel * gauss(rng));

    // 正解の F0（フレームごと）
    for (std::size_t fr = 0; fr < frames; ++fr)
    {
        const auto i = static_cast<std::size_t>(static_cast<double>(fr) * 0.005 * sr);
        s.f0PerFrame[fr] = i < n ? f0[i] : 0.0;
    }

    return s;
}

struct Metrics
{
    double rpa = 0.0;
    double octaveError = 0.0;
    double conpF = 0.0;
    double conpPrecision = 0.0;
    double conpRecall = 0.0;
};

Metrics evaluate(const Singer& singer)
{
    analysis::YinPitchAnalyzer analyzer;
    analysis::RuleBasedNoteSegmenter segmenter;
    const analysis::MonoAudioView view { singer.audio.data(), singer.audio.size(), singer.sampleRate };
    const auto track = analyzer.analyze(view, {});
    const auto estimated = segmenter.segment(track, view);

    Metrics m;

    // RPA・オクターブ誤り
    int refVoiced = 0, correct = 0, octave = 0;
    for (std::size_t f = 0; f < singer.f0PerFrame.size() && f < track.size(); ++f)
    {
        if (singer.f0PerFrame[f] <= 0.0)
            continue;

        ++refVoiced;

        if (! track.isVoiced(f))
            continue;

        const double diff = cents(track.f0Hz[f], singer.f0PerFrame[f]);

        if (std::abs(diff) <= 50.0)
            ++correct;
        else
        {
            const double r = std::round(diff / 1200.0);
            if (r != 0.0 && std::abs(diff - r * 1200.0) <= 50.0)
                ++octave;
        }
    }

    m.rpa = refVoiced > 0 ? static_cast<double>(correct) / refVoiced : 0.0;
    m.octaveError = refVoiced > 0 ? static_cast<double>(octave) / refVoiced : 0.0;

    // COnP: 開始時刻 ±50 ms・音高 ±50 セントで 1 対 1 に対応付ける
    std::vector<char> used(estimated.size(), 0);
    int matched = 0;

    for (const auto& ref : singer.notes)
    {
        int best = -1;
        double bestDiff = 1.0e9;

        for (std::size_t i = 0; i < estimated.size(); ++i)
        {
            if (used[i])
                continue;

            const double onset = std::abs(estimated[i].startSec - ref.start);
            const double pitch = std::abs(cents(440.0 * std::pow(2.0, (estimated[i].detectedMidi - 69.0) / 12.0), ref.hz));

            if (onset <= 0.05 && pitch <= 50.0 && onset < bestDiff)
            {
                best = static_cast<int>(i);
                bestDiff = onset;
            }
        }

        if (best >= 0)
        {
            used[static_cast<std::size_t>(best)] = 1;
            ++matched;
        }
    }

    m.conpPrecision = estimated.empty() ? 0.0 : static_cast<double>(matched) / static_cast<double>(estimated.size());
    m.conpRecall = singer.notes.empty() ? 0.0 : static_cast<double>(matched) / static_cast<double>(singer.notes.size());
    m.conpF = (m.conpPrecision + m.conpRecall) > 0.0 ? 2.0 * m.conpPrecision * m.conpRecall / (m.conpPrecision + m.conpRecall) : 0.0;
    return m;
}
} // namespace

TEST_CASE("合成した歌声で、RPA・オクターブ誤り・COnP が目標を満たす")
{
    double rpaSum = 0.0, octaveSum = 0.0, fSum = 0.0;
    int count = 0;

    for (const double sr : { 44100.0, 48000.0 })
    {
        for (unsigned seed = 1; seed <= 6; ++seed)
        {
            const auto singer = makeSinger(seed, sr, 24, false);
            const auto m = evaluate(singer);

            std::cout << "sr " << sr << " seed " << seed << ": RPA " << m.rpa << "  octave " << m.octaveError << "  COnP F " << m.conpF
                      << " (P " << m.conpPrecision << ", R " << m.conpRecall << ")\n";

            rpaSum += m.rpa;
            octaveSum += m.octaveError;
            fSum += m.conpF;
            ++count;
        }
    }

    const double rpa = rpaSum / count, octave = octaveSum / count, f = fSum / count;
    std::cout << "mean: RPA " << rpa << "  octave " << octave << "  COnP F " << f << "\n";

    CHECK(rpa >= 0.95);
    CHECK(octave < 0.01);
    CHECK(f >= 0.80);
}

TEST_CASE("背景ノイズが大きい合成歌声でも、精度が大きく落ちない")
{
    double rpaSum = 0.0, fSum = 0.0;
    for (unsigned seed = 11; seed <= 14; ++seed)
    {
        const auto m = evaluate(makeSinger(seed, 44100.0, 24, true));
        std::cout << "noisy seed " << seed << ": RPA " << m.rpa << "  octave " << m.octaveError << "  COnP F " << m.conpF << "\n";
        rpaSum += m.rpa;
        fSum += m.conpF;
    }

    CHECK(rpaSum / 4.0 >= 0.90);
    CHECK(fSum / 4.0 >= 0.70);
}

TEST_CASE("低い声（D2〜G3）と高い声（C5〜C6）でも、RPA・オクターブ誤り・COnP が目標を満たす")
{
    for (const auto& range : { std::pair<double, double> { 38.0, 55.0 }, std::pair<double, double> { 72.0, 84.0 } })
    {
        double rpaSum = 0.0, octaveSum = 0.0, fSum = 0.0;
        for (unsigned seed = 21; seed <= 24; ++seed)
        {
            const auto m = evaluate(makeSinger(seed, 44100.0, 24, false, range.first, range.second));
            rpaSum += m.rpa;
            octaveSum += m.octaveError;
            fSum += m.conpF;
        }

        std::cout << "range " << range.first << "-" << range.second << ": RPA " << rpaSum / 4.0 << "  octave " << octaveSum / 4.0 << "  COnP F " << fSum / 4.0 << '\n';
        CHECK(rpaSum / 4.0 >= 0.95);
        CHECK(octaveSum / 4.0 < 0.01);
        CHECK(fSum / 4.0 >= 0.80);
    }
}
