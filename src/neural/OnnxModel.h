#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace koenote::neural
{
// ONNX Runtime を実行時に読み込んで推論する薄いラッパー（要件 F-17 の任意機能）。
//
// onnxruntime.dll は本体に同梱せず、利用者が配置したものを絶対パスで LoadLibrary する
// （Windows 標準の onnxruntime.dll と取り違えないため）。ヘッダは third_party/onnxruntime（MIT）。
// 非 Windows ではロードに失敗する。
class OnnxModel
{
public:
    struct Input
    {
        std::string name;
        std::vector<std::int64_t> shape;
        const float* data = nullptr;
    };

    ~OnnxModel();
    OnnxModel(const OnnxModel&) = delete;
    OnnxModel& operator=(const OnnxModel&) = delete;

    // dllPath の onnxruntime.dll を読み込み、modelPath のモデルでセッションを作る。失敗時は nullptr と error。
    static std::unique_ptr<OnnxModel> load(const std::filesystem::path& dllPath,
                                           const std::filesystem::path& modelPath,
                                           int numThreads,
                                           std::string& error);

    const std::vector<std::string>& inputNames() const noexcept { return inputs; }
    const std::vector<std::string>& outputNames() const noexcept { return outputs; }

    // 最初の出力を float 配列として得る。成功すれば true。
    bool run(const std::vector<Input>& in, std::vector<float>& out, std::vector<std::int64_t>& outShape, std::string& error);

private:
    struct Impl;
    explicit OnnxModel(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
};
} // namespace koenote::neural
