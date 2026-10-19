#include "analysis/RuleBasedNoteSegmenter.h"
#include "analysis/YinPitchAnalyzer.h"
#include "model/EditModel.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

using namespace koenote::analysis;
using namespace koenote::model;

namespace
{
constexpr double kSampleRates[] = { 44100.0, 48000.0 };

double centsBetween(double f, double ref)
{
    return 1200.0 * std::log2(f / ref);
}

double midiOf(double hz)
{
    return 69.0 + 12.0 * std::log2(hz / 440.0);
}

std::size_t countSamples(double sr, double sec)
{
    return static_cast<std::size_t>(std::llround(sr * sec));
}

void appendSilence(std::vector<float>& out, double sr, double sec)
{
    out.insert(out.end(), countSamples(sr, sec), 0.0f);
}

// 位相連続の正弦波。freqAt(t) で瞬時周波数（Hz）を与える
template <typename FreqFn>
void appendSine(std::vector<float>& out, double sr, double sec, float amp, FreqFn freqAt)
{
    const std::size_t n = countSamples(sr, sec);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / sr;
        out.push_back(amp * static_cast<float>(std::sin(phase)));
        phase += 2.0 * std::numbers::pi * freqAt(t) / sr;
    }
}

std::vector<float> makeSine(double sr, double sec, double freqHz, float amp)
{
    std::vector<float> x;
    appendSine(x, sr, sec, amp, [freqHz](double) { return freqHz; });
    return x;
}

// 立ち上がりの鋭い鋸歯状波（高調波が豊富）
std::vector<float> makeSawtooth(double sr, double sec, double freqHz, float amp)
{
    const std::size_t n = countSamples(sr, sec);
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double phase = std::fmod(static_cast<double>(i) * freqHz / sr, 1.0);
        x[i] = amp * static_cast<float>(2.0 * phase - 1.0);
    }
    return x;
}

// 中心 centerHz、±50 セント、5.5 Hz のビブラート
std::vector<float> makeVibrato(double sr, double sec, double centerHz)
{
    std::vector<float> x;
    appendSine(x, sr, sec, 0.5f, [centerHz](double t) {
        return centerHz * std::pow(2.0, (50.0 / 1200.0) * std::sin(2.0 * std::numbers::pi * 5.5 * t));
    });
    return x;
}

PitchTrack analyzeSignal(const std::vector<float>& x, double sr)
{
    YinPitchAnalyzer analyzer;
    return analyzer.analyze(MonoAudioView{ x.data(), x.size(), sr }, {});
}

std::vector<Note> segmentSignal(const PitchTrack& track, const std::vector<float>& x, double sr)
{
    RuleBasedNoteSegmenter segmenter;
    return segmenter.segment(track, MonoAudioView{ x.data(), x.size(), sr });
}
} // namespace

TEST_CASE("220 Hz 正弦波の F0 は内部の有声フレームで ±20 セント以内に入る")
{
    for (const double sr : kSampleRates)
    {
        INFO("sampleRate = " << sr);
        const auto x = makeSine(sr, 1.0, 220.0, 0.5f);
        const auto track = analyzeSignal(x, sr);
        REQUIRE(track.size() == static_cast<std::size_t>(std::floor(1.0 / track.hopSec)) + 1);

        std::size_t internal = 0;
        std::size_t voiced = 0;
        std::size_t accurate = 0;
        for (std::size_t i = 0; i < track.size(); ++i)
        {
            const double t = track.timeOf(i);
            if (t < 0.05 || t > 0.95)
            {
                continue;
            }
            ++internal;
            if (!track.isVoiced(i))
            {
                continue;
            }
            ++voiced;
            if (std::abs(centsBetween(track.f0Hz[i], 220.0)) <= 20.0)
            {
                ++accurate;
            }
        }
        REQUIRE(voiced > 0);
        CHECK(static_cast<double>(voiced) >= 0.95 * static_cast<double>(internal));
        CHECK(static_cast<double>(accurate) >= 0.95 * static_cast<double>(voiced));
    }
}

TEST_CASE("150 Hz 鋸歯状波のオクターブ誤りは 1% 未満")
{
    for (const double sr : kSampleRates)
    {
        INFO("sampleRate = " << sr);
        const auto x = makeSawtooth(sr, 1.0, 150.0, 0.5f);
        const auto track = analyzeSignal(x, sr);

        std::size_t voiced = 0;
        std::size_t octaveErrors = 0;
        for (std::size_t i = 0; i < track.size(); ++i)
        {
            if (!track.isVoiced(i))
            {
                continue;
            }
            ++voiced;
            if (std::abs(centsBetween(track.f0Hz[i], 150.0)) > 600.0)
            {
                ++octaveErrors;
            }
        }
        REQUIRE(voiced > 0);
        CHECK(static_cast<double>(octaveErrors) < 0.01 * static_cast<double>(voiced));
    }
}

TEST_CASE("無音・正弦波・無音の信号で、無音区間は無声になり有声区間が検出される")
{
    for (const double sr : kSampleRates)
    {
        INFO("sampleRate = " << sr);
        std::vector<float> x;
        appendSilence(x, sr, 0.25);
        appendSine(x, sr, 0.5, 0.5f, [](double) { return 220.0; });
        appendSilence(x, sr, 0.25);
        const auto track = analyzeSignal(x, sr);

        std::size_t silentViolations = 0;
        std::size_t midVoiced = 0;
        std::size_t midTotal = 0;
        for (std::size_t i = 0; i < track.size(); ++i)
        {
            const double t = track.timeOf(i);
            if (t < 0.2 || t > 0.8)
            {
                if (track.f0Hz[i] != 0.0f)
                {
                    ++silentViolations;
                }
            }
            else if (t >= 0.3 && t <= 0.7)
            {
                ++midTotal;
                if (track.isVoiced(i))
                {
                    ++midVoiced;
                }
            }
        }
        CHECK(silentViolations == 0);
        REQUIRE(midTotal > 0);
        CHECK(static_cast<double>(midVoiced) >= 0.9 * static_cast<double>(midTotal));
    }
}

TEST_CASE("ビブラートのある 2 秒の音は 1 ノートになり、中心の MIDI 値に一致する")
{
    for (const double sr : kSampleRates)
    {
        INFO("sampleRate = " << sr);
        const auto x = makeVibrato(sr, 2.0, 330.0);
        const auto track = analyzeSignal(x, sr);
        const auto notes = segmentSignal(track, x, sr);

        REQUIRE(notes.size() == 1);
        CHECK(std::abs(static_cast<double>(notes[0].detectedMidi) - midiOf(330.0)) <= 0.3);
        CHECK(notes[0].endSec - notes[0].startSec > 1.5);
    }
}

TEST_CASE("C4 E4 G4 の階段信号は 3 ノートに分割され、各 detectedMidi が正しい")
{
    for (const double sr : kSampleRates)
    {
        INFO("sampleRate = " << sr);
        std::vector<float> x;
        appendSilence(x, sr, 0.1);
        appendSine(x, sr, 0.5, 0.5f, [](double) { return 261.63; });
        appendSilence(x, sr, 0.03);
        appendSine(x, sr, 0.5, 0.5f, [](double) { return 329.63; });
        appendSilence(x, sr, 0.03);
        appendSine(x, sr, 0.5, 0.5f, [](double) { return 392.0; });
        appendSilence(x, sr, 0.1);

        const auto track = analyzeSignal(x, sr);
        const auto notes = segmentSignal(track, x, sr);

        REQUIRE(notes.size() == 3);
        CHECK(std::abs(static_cast<double>(notes[0].detectedMidi) - 60.0) <= 0.3);
        CHECK(std::abs(static_cast<double>(notes[1].detectedMidi) - 64.0) <= 0.3);
        CHECK(std::abs(static_cast<double>(notes[2].detectedMidi) - 67.0) <= 0.3);

        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            CHECK(notes[i].endSec - notes[i].startSec >= EditModel::kMinNoteDurationSec);
            CHECK(notes[i].level >= 0.0f);
            CHECK(notes[i].level <= 1.0f);
            if (i > 0)
            {
                CHECK(notes[i].startSec >= notes[i - 1].endSec);
            }
        }
    }
}

TEST_CASE("ProgressCallback が false を返すと空の PitchTrack を返す")
{
    const auto x = makeSine(44100.0, 1.0, 220.0, 0.5f);
    YinPitchAnalyzer analyzer;
    int calls = 0;
    const auto track = analyzer.analyze(MonoAudioView{ x.data(), x.size(), 44100.0 }, [&calls](float) {
        ++calls;
        return false; // 最初の呼び出しで中断
    });
    CHECK(calls >= 1);
    CHECK(track.empty());
}

TEST_CASE("ProgressCallback が空でも解析でき、進捗は 0..1 で通知される")
{
    const auto x = makeSine(44100.0, 1.0, 220.0, 0.5f);
    YinPitchAnalyzer analyzer;
    const auto track = analyzer.analyze(MonoAudioView{ x.data(), x.size(), 44100.0 }, {});
    CHECK(track.size() > 0);

    bool inRange = true;
    const auto track2 = analyzer.analyze(MonoAudioView{ x.data(), x.size(), 44100.0 }, [&inRange](float p) {
        if (p < 0.0f || p > 1.0f)
        {
            inRange = false;
        }
        return true;
    });
    CHECK(inRange);
    CHECK(track2.size() == track.size());
}

TEST_CASE("空入力は空の結果を返す")
{
    YinPitchAnalyzer analyzer;
    const float dummy = 0.1f;
    CHECK(analyzer.analyze(MonoAudioView{}, {}).empty());
    CHECK(analyzer.analyze(MonoAudioView{ &dummy, 0, 44100.0 }, {}).empty());
    CHECK(analyzer.analyze(MonoAudioView{ &dummy, 1, 0.0 }, {}).empty());

    RuleBasedNoteSegmenter segmenter;
    CHECK(segmenter.segment(PitchTrack{}, MonoAudioView{}).empty());
}

TEST_CASE("10 秒の信号の解析は 10 秒未満で完了する")
{
    const auto x = makeVibrato(48000.0, 10.0, 220.0);
    const auto start = std::chrono::steady_clock::now();
    const auto track = analyzeSignal(x, 48000.0);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(track.size() > 0);
    CHECK(std::chrono::duration<double>(elapsed).count() < 10.0);
}
