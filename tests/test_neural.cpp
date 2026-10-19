// ニューラル加工（F-17）の前処理（メルスペクトログラム、リサンプル、設定の読み込み）の単体テスト。
// 環境変数 KOENOTE_NEURAL_DIR（vocoder.yaml と onnxruntime.dll があるディレクトリ）が設定されている場合だけ、
// 実際のモデルによる合成テストも実行する（モデルの重みはリポジトリに含めないため、通常は実行されない）。

#include "analysis/RuleBasedNoteSegmenter.h"
#include "analysis/YinPitchAnalyzer.h"
#include "dsp/Fft.h"
#include "dsp/Resample.h"
#include "neural/MelSpectrogram.h"
#include "neural/NeuralRenderer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <vector>

using namespace koenote;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
std::vector<float> sine(double hz, double sr, double seconds)
{
    std::vector<float> x(static_cast<std::size_t>(sr * seconds));
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / sr));
    return x;
}

double zeroCrossingFrequency(const std::vector<float>& x, double sr, double from, double to)
{
    const auto i0 = static_cast<std::size_t>(from * sr);
    const auto i1 = std::min(x.size(), static_cast<std::size_t>(to * sr));
    double first = -1.0, last = -1.0;
    int count = 0;
    for (auto i = i0 + 1; i < i1; ++i)
    {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double pos = static_cast<double>(i - 1) - static_cast<double>(x[i - 1]) / static_cast<double>(x[i] - x[i - 1]);
            if (first < 0.0)
                first = pos;
            last = pos;
            ++count;
        }
    }
    return count < 3 ? 0.0 : static_cast<double>(count - 1) * sr / (last - first);
}
} // namespace

TEST_CASE("FFT は単一の正弦波のビンにエネルギーが集まる")
{
    constexpr std::size_t n = 1024;
    dsp::Fft fft(n);
    std::vector<std::complex<double>> data(n);
    for (std::size_t i = 0; i < n; ++i)
        data[i] = std::sin(2.0 * std::numbers::pi * 64.0 * static_cast<double>(i) / static_cast<double>(n));

    fft.forward(data.data());

    std::size_t peak = 0;
    for (std::size_t k = 1; k < n / 2; ++k)
        if (std::abs(data[k]) > std::abs(data[peak]))
            peak = k;

    CHECK(peak == 64);
    CHECK_THAT(std::abs(data[64]), WithinRel(static_cast<double>(n) / 2.0, 1e-9));
}

TEST_CASE("リサンプルは周波数と長さを保つ")
{
    const auto in = sine(1000.0, 48000.0, 1.0);
    const auto out = dsp::resample(in, 48000.0, 44100.0);

    CHECK(out.size() == 44100);
    CHECK_THAT(zeroCrossingFrequency(out, 44100.0, 0.2, 0.8), WithinRel(1000.0, 0.002));

    // 振幅も保たれる（端を除く）
    float peak = 0.0f;
    for (std::size_t i = 4410; i < 39690; ++i)
        peak = std::max(peak, std::abs(out[i]));
    CHECK_THAT(static_cast<double>(peak), WithinAbs(0.5, 0.01));

    // 往復しても元に近い
    const auto back = dsp::resample(out, 44100.0, 48000.0);
    REQUIRE(back.size() == in.size());
    double error = 0.0;
    for (std::size_t i = 4800; i < 43200; ++i)
        error = std::max(error, static_cast<double>(std::abs(back[i] - in[i])));
    CHECK(error < 0.02);
}

TEST_CASE("ダウンサンプルは Nyquist より上の成分を除去する")
{
    const auto in = sine(15000.0, 48000.0, 0.5); // 22050 Hz へ下げると Nyquist（11025 Hz）を超える
    const auto out = dsp::resample(in, 48000.0, 22050.0);

    float peak = 0.0f;
    for (std::size_t i = 1000; i + 1000 < out.size(); ++i)
        peak = std::max(peak, std::abs(out[i]));
    CHECK(peak < 0.02f);
}

TEST_CASE("リサンプルの異常入力")
{
    CHECK(dsp::resample({}, 48000.0, 44100.0).empty());
    CHECK(dsp::resample({ 1.0f }, 0.0, 44100.0).empty());
    CHECK(dsp::resample({ 1.0f, 2.0f }, 44100.0, 44100.0).size() == 2);
}

TEST_CASE("メル尺度の変換は往復できる")
{
    for (const double hz : { 40.0, 200.0, 999.0, 1000.0, 3000.0, 16000.0 })
        CHECK_THAT(neural::melToHz(neural::hzToMel(hz)), WithinRel(hz, 1e-9));

    CHECK_THAT(neural::hzToMel(1000.0), WithinAbs(15.0, 1e-9)); // Slaney: 1 kHz = 15 mel
}

TEST_CASE("対数メルのピークは正弦波の周波数に対応するビンに出る")
{
    neural::MelConfig config;
    const auto wave = sine(1000.0, config.sampleRate, 1.0);

    std::size_t frames = 0;
    const auto mel = neural::computeLogMel(wave, config, frames);

    CHECK(frames == (wave.size() + 511) / 512);
    REQUIRE(mel.size() == frames * static_cast<std::size_t>(config.numMelBins));

    const auto centres = neural::melBinCentreHz(config);
    REQUIRE(centres.size() == 128);

    // 中央付近のフレームで最大のビンの中心周波数が 1 kHz 付近
    const std::size_t frame = frames / 2;
    std::size_t best = 0;
    for (std::size_t b = 0; b < 128; ++b)
        if (mel[frame * 128 + b] > mel[frame * 128 + best])
            best = b;

    CHECK_THAT(centres[best], WithinRel(1000.0, 0.08));

    // 無音の対数メルは下限（log(1e-5)）付近
    std::size_t silentFrames = 0;
    const auto silent = neural::computeLogMel(std::vector<float>(44100, 0.0f), config, silentFrames);
    CHECK(silent[0] < -11.0f);
}

TEST_CASE("メルのフォルマントシフトは周波数軸を動かす")
{
    neural::MelConfig config;
    const auto centres = neural::melBinCentreHz(config);

    // 1 kHz 付近にだけ山があるフレーム
    std::vector<float> frame(128, -10.0f);
    std::size_t peak = 0;
    for (std::size_t b = 0; b < 128; ++b)
        if (std::abs(centres[b] - 1000.0) < std::abs(centres[peak] - 1000.0))
            peak = b;
    frame[peak] = 0.0f;

    std::vector<float> shifted(128);
    neural::shiftMelFrame(frame.data(), shifted.data(), centres, 2.0); // 1 オクターブ上

    std::size_t newPeak = 0;
    for (std::size_t b = 0; b < 128; ++b)
        if (shifted[b] > shifted[newPeak])
            newPeak = b;

    CHECK_THAT(centres[newPeak], WithinRel(2000.0, 0.1));

    // factor = 1 は恒等
    std::vector<float> same(128);
    neural::shiftMelFrame(frame.data(), same.data(), centres, 1.0);
    for (std::size_t b = 0; b < 128; ++b)
        CHECK_THAT(static_cast<double>(same[b]), WithinAbs(static_cast<double>(frame[b]), 1e-4));
}

TEST_CASE("vocoder.yaml の読み込み")
{
    const std::string yaml =
        "name: \"test_vocoder\"\n"
        "model: \"test.onnx\"\n"
        "sample_rate: 44100\n"
        "hop_size: 512\n"
        "win_size: 2048\n"
        "fft_size: 2048\n"
        "num_mel_bins: 128\n"
        "mel_fmin: 40\n"
        "mel_fmax: 16000\n"
        "pitch_controllable: true\n";

    const auto p = neural::parseVocoderYaml(yaml, "dir");
    REQUIRE(p.available);
    CHECK(p.name == "test_vocoder");
    CHECK(p.mel.hopSize == 512);
    CHECK(p.mel.numMelBins == 128);
    CHECK(p.mel.fmax == 16000.0);
    CHECK(p.modelFile.filename() == "test.onnx");

    CHECK_FALSE(neural::parseVocoderYaml("name: x\n", "dir").available);
    CHECK_FALSE(neural::parseVocoderYaml("model: a.onnx\nsample_rate: 0\nhop_size: 512\nwin_size: 2048\nfft_size: 2048\nnum_mel_bins: 128\nmel_fmin: 40\nmel_fmax: 16000\n", "d").available);
}

TEST_CASE("ボコーダーのパッケージが無い場合は利用不可になる")
{
    const auto p = neural::findVocoderPackage({ "this/directory/does/not/exist" });
    CHECK_FALSE(p.available);
    CHECK_FALSE(p.reason.empty());
}

// --- 以下は、実際のモデルが利用できる環境（KOENOTE_NEURAL_DIR）でのみ実行する ---
namespace
{
void checkRealModel(double sampleRate)
{
    const char* dir = std::getenv("KOENOTE_NEURAL_DIR");
    if (dir == nullptr)
    {
        SUCCEED("KOENOTE_NEURAL_DIR が未設定のため、実モデルのテストは省略した");
        return;
    }

    const auto package = neural::findVocoderPackage({ dir });
    INFO(package.reason);
    REQUIRE(package.available);

    std::string error;
    auto vocoder = neural::NsfHifiGanVocoder::create(package, 4, error);
    INFO(error);
    REQUIRE(vocoder != nullptr);

    // 声に近い合成音（220 Hz 1 秒 → 無音 → 261.63 Hz 1 秒、44.1 kHz）
    const double sr = sampleRate;
    std::vector<float> voice(static_cast<std::size_t>(3.0 * sr), 0.0f);
    double phase = 0.0;
    for (std::size_t i = 0; i < voice.size(); ++i)
    {
        const double t = static_cast<double>(i) / sr;
        const bool a = t >= 0.3 && t < 1.3;
        const bool b = t >= 1.6 && t < 2.6;
        if (! a && ! b)
            continue;
        phase += 2.0 * std::numbers::pi * (a ? 220.0 : 261.63) / sr;
        double v = 0.0;
        for (int h = 1; h <= 12; ++h)
            v += std::sin(phase * h) / (h * 1.2);
        voice[i] = static_cast<float>(0.3 * v * std::min({ 1.0, (t - (a ? 0.3 : 1.6)) / 0.02, ((a ? 1.3 : 2.6) - t) / 0.03 }));
    }

    const analysis::MonoAudioView view { voice.data(), voice.size(), sr };
    analysis::YinPitchAnalyzer analyzer;
    analysis::RuleBasedNoteSegmenter segmenter;
    auto track = analyzer.analyze(view, {});
    auto notes = segmenter.segment(track, view);
    REQUIRE(notes.size() == 2);

    for (std::size_t i = 0; i < notes.size(); ++i)
        notes[i].id = i + 1;
    notes[0].edit.pitchOffsetCents = 300.0f;                  // +3 半音
    notes[1].edit.targetStartSec = notes[1].startSec + 0.3;   // 0.3 秒遅らせる
    notes[1].edit.targetEndSec = notes[1].endSec + 0.3;

    const float* ptr = voice.data();
    render::RenderInput input;
    input.channels = &ptr;
    input.numChannels = 1;
    input.numFrames = static_cast<std::int64_t>(voice.size());
    input.sampleRate = sr;
    input.track = &track;
    input.notes = &notes;

    const auto begin = std::chrono::steady_clock::now();
    const auto out = neural::renderNeural(input, *vocoder, {}, error);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    std::cout << "neural render: " << elapsed << " s for 3 s of audio\n";

    INFO(error);
    REQUIRE(out.numChannels == 1);
    CHECK_THAT(static_cast<double>(out.numFrames()) / sr, WithinAbs(3.3, 0.05));

    const analysis::MonoAudioView outView { out.channels[0].data(), out.channels[0].size(), sr };
    const auto edited = analyzer.analyze(outView, {});

    const auto medianF0 = [&] (double from, double to)
    {
        std::vector<double> v;
        for (std::size_t f = 0; f < edited.size(); ++f)
            if (edited.timeOf(f) >= from && edited.timeOf(f) < to && edited.isVoiced(f))
                v.push_back(edited.f0Hz[f]);
        std::sort(v.begin(), v.end());
        return v.empty() ? 0.0 : v[v.size() / 2];
    };

    // 1 つ目: 220 Hz → +300 セント
    const double expectedFirst = 220.0 * std::pow(2.0, 300.0 / 1200.0);
    const double firstCents = 1200.0 * std::log2(medianF0(0.6, 1.2) / expectedFirst);
    std::cout << "first note: median f0 " << medianF0(0.6, 1.2) << " Hz, expected " << expectedFirst << ", error " << firstCents << " cents\n";
    CHECK(std::abs(firstCents) < 50.0);

    // 2 つ目: 261.63 Hz のまま、0.3 秒遅れる（1.9〜2.9 秒）
    const double secondCents = 1200.0 * std::log2(medianF0(2.1, 2.7) / 261.63);
    std::cout << "second note: median f0 " << medianF0(2.1, 2.7) << " Hz, error " << secondCents << " cents\n";
    CHECK(std::abs(secondCents) < 50.0);
}
} // namespace

TEST_CASE("実モデル: ピッチ編集と時間伸縮を反映した音声を合成できる（44.1 kHz）")
{
    checkRealModel(44100.0);
}

TEST_CASE("実モデル: ピッチ編集と時間伸縮を反映した音声を合成できる（48 kHz。リサンプルを含む）")
{
    checkRealModel(48000.0);
}
