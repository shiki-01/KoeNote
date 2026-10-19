#include "neural/NeuralRenderer.h"

#include "dsp/Resample.h"
#include "render/GainEnvelope.h"
#include "render/PitchCurveEditor.h"
#include "render/TimeMap.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace koenote::neural
{
namespace
{
std::string trim(const std::string& s)
{
    const auto b = s.find_first_not_of(" \t\r\n\"'");
    if (b == std::string::npos)
        return {};
    const auto e = s.find_last_not_of(" \t\r\n\"'");
    return s.substr(b, e - b + 1);
}

} // namespace

VocoderPackage parseVocoderYaml(const std::string& yamlText, const std::filesystem::path& directory)
{
    VocoderPackage p;
    std::string modelName;
    std::istringstream stream(yamlText);
    std::string line;

    while (std::getline(stream, line))
    {
        const auto colon = line.find(':');
        if (colon == std::string::npos || trim(line).rfind('#', 0) == 0)
            continue;

        const auto key = trim(line.substr(0, colon));
        const auto value = trim(line.substr(colon + 1));

        if (key == "name") p.name = value;
        else if (key == "model") modelName = value;
        else if (key == "sample_rate") p.mel.sampleRate = std::atof(value.c_str());
        else if (key == "hop_size") p.mel.hopSize = std::atoi(value.c_str());
        else if (key == "win_size") p.mel.winSize = std::atoi(value.c_str());
        else if (key == "fft_size") p.mel.fftSize = std::atoi(value.c_str());
        else if (key == "num_mel_bins") p.mel.numMelBins = std::atoi(value.c_str());
        else if (key == "mel_fmin") p.mel.fmin = std::atof(value.c_str());
        else if (key == "mel_fmax") p.mel.fmax = std::atof(value.c_str());
        else if (key == "pitch_controllable") p.pitchControllable = value == "true";
    }

    if (modelName.empty())
    {
        p.reason = "vocoder.yaml has no model entry";
        return p;
    }

    if (p.mel.sampleRate <= 0.0 || p.mel.hopSize <= 0 || p.mel.winSize <= 0 || p.mel.fftSize < p.mel.winSize || p.mel.numMelBins <= 0
        || ! (p.mel.fmax > p.mel.fmin))
    {
        p.reason = "vocoder.yaml has invalid mel settings";
        return p;
    }

    p.modelFile = directory / modelName;
    p.available = true;
    return p;
}

VocoderPackage findVocoderPackage(const std::vector<std::filesystem::path>& directories)
{
    VocoderPackage result;
    result.reason = "vocoder package not found";

    for (const auto& dir : directories)
    {
        std::error_code ec;
        const auto yaml = dir / "vocoder.yaml";
        if (! std::filesystem::exists(yaml, ec))
            continue;

        std::ifstream file(yaml);
        std::stringstream text;
        text << file.rdbuf();

        auto package = parseVocoderYaml(text.str(), dir);

        if (! package.available)
        {
            result = package;
            continue;
        }

        if (! std::filesystem::exists(package.modelFile, ec))
        {
            result = {};
            result.reason = "model file not found: " + package.modelFile.string();
            continue;
        }

        // onnxruntime.dll は、同じディレクトリ、runtime/、親ディレクトリの順に探す
        for (const auto& candidate : { dir / "onnxruntime.dll", dir / "runtime" / "onnxruntime.dll", dir.parent_path() / "onnxruntime.dll" })
        {
            if (std::filesystem::exists(candidate, ec))
            {
                package.runtimeDll = candidate;
                return package;
            }
        }

        result = {};
        result.reason = "onnxruntime.dll not found next to the vocoder";
    }

    return result;
}

//==============================================================================
std::unique_ptr<NsfHifiGanVocoder> NsfHifiGanVocoder::create(const VocoderPackage& package, int numThreads, std::string& error)
{
    if (! package.available)
    {
        error = package.reason;
        return nullptr;
    }

    auto model = OnnxModel::load(package.runtimeDll, package.modelFile, numThreads, error);
    if (model == nullptr)
        return nullptr;

    std::unique_ptr<NsfHifiGanVocoder> v(new NsfHifiGanVocoder(std::move(model), package.mel));

    // 入力名: 名前に mel / f0 を含むものを使う。見つからなければ 1 番目が mel、2 番目が f0。
    const auto& names = v->model->inputNames();
    for (const auto& n : names)
    {
        if (n.find("mel") != std::string::npos)
            v->melInputName = n;
        else if (n.find("f0") != std::string::npos)
            v->f0InputName = n;
    }

    if (v->melInputName.empty() && names.size() >= 1)
        v->melInputName = names[0];
    if (v->f0InputName.empty() && names.size() >= 2)
        v->f0InputName = names[1];

    if (v->melInputName.empty() || v->f0InputName.empty())
    {
        error = "unexpected model inputs";
        return nullptr;
    }

    return v;
}

bool NsfHifiGanVocoder::synthesize(const std::vector<float>& mel,
                                   const std::vector<float>& f0,
                                   std::size_t frames,
                                   std::vector<float>& wave,
                                   const std::function<bool(float)>& progress,
                                   std::string& error)
{
    const auto bins = static_cast<std::size_t>(config.numMelBins);
    const auto hop = static_cast<std::size_t>(config.hopSize);

    if (mel.size() != frames * bins || f0.size() != frames)
    {
        error = "mel/f0 size mismatch";
        return false;
    }

    wave.assign(frames * hop, 0.0f);

    // 畳み込みで受容野が有限なため、チャンクの前後に context を付けて処理し、中央だけを使う。
    constexpr std::size_t kChunk = 400;
    constexpr std::size_t kContext = 48;

    std::vector<float> outChunk;
    std::vector<std::int64_t> outShape;

    for (std::size_t begin = 0; begin < frames; begin += kChunk)
    {
        if (progress && ! progress(static_cast<float>(begin) / static_cast<float>(frames)))
            return false;

        const auto end = std::min(frames, begin + kChunk);
        const auto from = begin > kContext ? begin - kContext : 0;
        const auto to = std::min(frames, end + kContext);
        const auto n = to - from;

        const std::vector<OnnxModel::Input> inputs {
            { melInputName, { 1, static_cast<std::int64_t>(n), static_cast<std::int64_t>(bins) }, mel.data() + from * bins },
            { f0InputName, { 1, static_cast<std::int64_t>(n) }, f0.data() + from },
        };

        if (! model->run(inputs, outChunk, outShape, error))
            return false;

        if (outChunk.size() < n * hop)
        {
            error = "unexpected vocoder output size";
            return false;
        }

        // 中央部（begin..end）だけをコピー
        for (std::size_t i = begin * hop; i < end * hop; ++i)
            wave[i] = outChunk[i - from * hop];
    }

    return true;
}

//==============================================================================
render::RenderedAudio renderNeural(const render::RenderInput& input,
                                   NsfHifiGanVocoder& vocoder,
                                   const render::RenderProgress& progress,
                                   std::string& error)
{
    render::RenderedAudio result;

    if (input.channels == nullptr || input.numChannels <= 0 || input.numFrames <= 0 || input.sampleRate <= 0.0)
    {
        error = "invalid input";
        return result;
    }

    const auto& mc = vocoder.melConfig();
    const double srcRate = input.sampleRate;
    const std::vector<model::Note> noNotes;
    const model::PitchTrack noTrack;
    const auto& notes = input.notes != nullptr ? *input.notes : noNotes;
    const auto& track = input.track != nullptr ? *input.track : noTrack;

    // 1. モノラル化してボコーダーのサンプルレートへ
    std::vector<float> mono(static_cast<std::size_t>(input.numFrames), 0.0f);
    const float monoGain = 1.0f / static_cast<float>(input.numChannels);
    for (int c = 0; c < input.numChannels; ++c)
        for (std::size_t i = 0; i < mono.size(); ++i)
            mono[i] += input.channels[c][i] * monoGain;

    const auto wave = dsp::resample(mono, srcRate, mc.sampleRate);
    mono.clear();

    if (progress && ! progress(0.02f))
        return result;

    // 2. 元音声の対数メル
    std::size_t srcFrames = 0;
    const auto srcMel = computeLogMel(wave, mc, srcFrames);

    if (srcFrames == 0)
    {
        error = "could not compute the mel spectrogram";
        return result;
    }

    if (progress && ! progress(0.06f))
        return result;

    // 3. 出力時間軸のフレーム列（時間伸縮・フォルマント・ピッチ）
    const auto timeMap = render::TimeMap::fromNotes(notes, 0.06, input.pins);
    const auto adjust = render::computePitchAdjustCents(track, notes);
    const auto formant = render::computeFormantShiftCents(track, notes, adjust);

    const double srcDuration = static_cast<double>(input.numFrames) / srcRate;
    const double outDuration = std::clamp(timeMap.toOutput(srcDuration), 0.0, srcDuration * 2.0 + 30.0);
    const auto bins = static_cast<std::size_t>(mc.numMelBins);
    const auto hop = static_cast<std::size_t>(mc.hopSize);
    const auto outFrames = static_cast<std::size_t>(std::ceil(outDuration * mc.sampleRate / static_cast<double>(hop))) + 1;
    const auto centres = melBinCentreHz(mc);

    constexpr float kSilence = -11.5129f; // log(1e-5)
    std::vector<float> mel(outFrames * bins, kSilence);
    std::vector<float> f0(outFrames, 0.0f);
    std::vector<float> shifted(bins);

    const auto frameValue = [&] (const std::vector<float>& values, double srcSec) -> float
    {
        if (values.empty())
            return 0.0f;
        const auto i = std::llround((srcSec - track.startSec) / track.hopSec);
        return (i < 0 || static_cast<std::size_t>(i) >= values.size()) ? 0.0f : values[static_cast<std::size_t>(i)];
    };

    for (std::size_t j = 0; j < outFrames; ++j)
    {
        const double outSec = (static_cast<double>(j) + 0.5) * static_cast<double>(hop) / mc.sampleRate;

        // 元音声を使わない区間（傾き 0）は無音のフレームのまま
        if (timeMap.slopeAt(outSec) < 0.02)
            continue;

        const double srcSec = timeMap.toSource(outSec);
        const double position = srcSec * mc.sampleRate / static_cast<double>(hop) - 0.5; // 元メルのフレーム位置

        if (position < -1.0 || position > static_cast<double>(srcFrames))
            continue;

        const auto lo = static_cast<std::ptrdiff_t>(std::floor(position));
        const double t = position - static_cast<double>(lo);
        const auto a = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(lo, 0, static_cast<std::ptrdiff_t>(srcFrames) - 1));
        const auto b = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(lo + 1, 0, static_cast<std::ptrdiff_t>(srcFrames) - 1));

        float* out = mel.data() + j * bins;
        for (std::size_t k = 0; k < bins; ++k)
            out[k] = static_cast<float>(srcMel[a * bins + k] * (1.0 - t) + srcMel[b * bins + k] * t);

        // フォルマントのシフト（メルの周波数軸をずらす）。±1 オクターブに制限する。
        const double formantCents = std::clamp(static_cast<double>(frameValue(formant, srcSec)), -1200.0, 1200.0);
        if (formantCents != 0.0)
        {
            shiftMelFrame(out, shifted.data(), centres, std::pow(2.0, formantCents / 1200.0));
            std::copy(shifted.begin(), shifted.end(), out);
        }

        // f0: 元の F0 に、編集のピッチ補正を掛ける。無声は 0。
        const auto trackFrame = std::llround((srcSec - track.startSec) / track.hopSec);
        if (trackFrame >= 0 && track.isVoiced(static_cast<std::size_t>(trackFrame)))
        {
            const double hz = static_cast<double>(track.f0Hz[static_cast<std::size_t>(trackFrame)])
                              * std::pow(2.0, static_cast<double>(frameValue(adjust, srcSec)) / 1200.0);
            f0[j] = static_cast<float>(std::clamp(hz, 30.0, 2000.0));
        }
    }

    // 4. ボコーダー
    std::vector<float> synthesised;
    const bool ok = vocoder.synthesize(mel, f0, outFrames, synthesised,
                                       [&] (float p) { return ! progress || progress(0.1f + 0.85f * p); }, error);

    if (! ok)
        return result; // 中断（error が空）または失敗

    // 5. 元のサンプルレートへ戻し、ゲイン曲線を掛ける
    auto out = dsp::resample(synthesised, mc.sampleRate, srcRate);
    const auto numOut = std::min(out.size(), static_cast<std::size_t>(std::ceil(outDuration * srcRate)) + 1);
    out.resize(numOut);

    const render::GainEnvelope gains(notes, outDuration);
    if (! gains.isUnity())
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] *= gains.gainAt(static_cast<double>(i) / srcRate);

    result.numChannels = input.numChannels;
    result.sampleRate = srcRate;
    result.channels.assign(static_cast<std::size_t>(input.numChannels), out);

    if (progress)
        progress(1.0f);

    return result;
}
} // namespace koenote::neural
