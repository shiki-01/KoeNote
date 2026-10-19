#pragma once

#include "neural/NeuralRenderer.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

namespace koenote::plugin
{
// ニューラルボコーダー（F-17）の探索と読み込み。プロセスで 1 つのモデルを共有する。
//
// 利用者が次のいずれかの場所に、vocoder.yaml・モデル（.onnx）と onnxruntime.dll を置くと利用できる。
//   1. %APPDATA%/KoeNote/neural/（直下、またはその 1 階層下のフォルダ）
//   2. プラグイン本体（.vst3 内の DLL）と同じフォルダの neural/
//   3. 環境変数 KOENOTE_NEURAL_DIR（開発用）
// 重みはライセンス（非商用条件など）の確認を利用者が行ったうえで配置する。本体には同梱しない。
class NeuralService
{
public:
    static NeuralService& get();

    // 探索結果（短時間はキャッシュする）。message thread / worker thread から呼べる。
    neural::VocoderPackage package();

    // ボコーダーを読み込んで返す（初回は数百 ms〜数秒）。worker thread で呼ぶこと。失敗時は nullptr と error。
    std::shared_ptr<neural::NsfHifiGanVocoder> vocoder(std::string& error);

    // 次回の package() で再探索させる
    void rescan();

private:
    NeuralService() = default;

    std::mutex mutex;     // 探索結果のキャッシュ用（短時間だけ保持する。UI スレッドからも使う）
    std::mutex loadMutex; // モデルの読み込み用（数秒かかることがあるため、mutex とは別にする）
    neural::VocoderPackage cachedPackage;
    std::chrono::steady_clock::time_point cachedAt;
    bool haveCache = false;

    std::shared_ptr<neural::NsfHifiGanVocoder> loaded;
    std::string loadedModel;
    std::string loadError;
};
} // namespace koenote::plugin
