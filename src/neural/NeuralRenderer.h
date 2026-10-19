#pragma once

#include "neural/MelSpectrogram.h"
#include "neural/OnnxModel.h"
#include "render/PsolaRenderer.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace koenote::neural
{
// 利用者が配置したニューラルボコーダー一式（onnxruntime.dll と、モデル・vocoder.yaml）の場所と設定。
struct VocoderPackage
{
    bool available = false;
    std::string reason;                  // 利用できない理由（available == false のとき）
    std::filesystem::path runtimeDll;    // onnxruntime.dll
    std::filesystem::path modelFile;     // *.onnx
    std::string name;                    // vocoder.yaml の name
    MelConfig mel;
    bool pitchControllable = true;
};

// directories を順に探して、最初に見つかった VocoderPackage を返す。
// 各ディレクトリの直下に onnxruntime.dll と vocoder.yaml（と、その model: が指す .onnx）が必要。
// onnxruntime.dll は、modelFile と別のディレクトリ（同階層の runtime/ やバンドルの隣など）にあってもよい。
VocoderPackage findVocoderPackage(const std::vector<std::filesystem::path>& directories);

// vocoder.yaml の「key: value」だけを読む簡易パーサー（テスト用に公開）。
VocoderPackage parseVocoderYaml(const std::string& yamlText, const std::filesystem::path& directory);

// NSF-HiFiGAN 系のボコーダー。mel と f0 から波形を作る。
class NsfHifiGanVocoder
{
public:
    // 読み込みに失敗したら nullptr と error
    static std::unique_ptr<NsfHifiGanVocoder> create(const VocoderPackage& package, int numThreads, std::string& error);

    const MelConfig& melConfig() const noexcept { return config; }

    // mel: frames * numMelBins（行優先）、f0: frames（Hz。無声は 0）。戻り値は frames * hop のサンプル。
    // 長い入力は context 付きのチャンクに分けて処理する。progress が false を返すと中断して false を返す。
    bool synthesize(const std::vector<float>& mel,
                    const std::vector<float>& f0,
                    std::size_t frames,
                    std::vector<float>& wave,
                    const std::function<bool(float)>& progress,
                    std::string& error);

private:
    NsfHifiGanVocoder(std::unique_ptr<OnnxModel> m, const MelConfig& c) : model(std::move(m)), config(c) {}

    std::unique_ptr<OnnxModel> model;
    MelConfig config;
    std::string melInputName, f0InputName;
};

// 編集内容（ピッチ・タイミング・フォルマント・ゲイン）を反映した音声を、ニューラルボコーダーで合成する（F-17）。
// 手順: 元音声（モノラル）→ 対数メル → 出力時間軸へ時間伸縮（TimeMap）・フォルマントシフト（メル軸のずらし）→
//       f0（編集後）とともにボコーダーへ → 元のサンプルレートへ戻してゲイン曲線を掛ける。
// 出力は、ボコーダーがモノラルのため全チャンネルに同じ波形を入れる。
// 中断・失敗時は numChannels == 0 の結果を返し、error に理由を入れる（中断のみの場合は空）。
render::RenderedAudio renderNeural(const render::RenderInput& input,
                                   NsfHifiGanVocoder& vocoder,
                                   const render::RenderProgress& progress,
                                   std::string& error);
} // namespace koenote::neural
