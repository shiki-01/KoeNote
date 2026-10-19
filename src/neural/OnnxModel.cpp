#include "neural/OnnxModel.h"

#include "onnxruntime/onnxruntime_c_api.h"

#if defined(_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

#include <algorithm>

namespace koenote::neural
{
struct OnnxModel::Impl
{
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtSessionOptions* options = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* memory = nullptr;

    ~Impl()
    {
        if (api == nullptr)
            return;
        if (session != nullptr)
            api->ReleaseSession(session);
        if (memory != nullptr)
            api->ReleaseMemoryInfo(memory);
        if (options != nullptr)
            api->ReleaseSessionOptions(options);
        if (env != nullptr)
            api->ReleaseEnv(env);
    }

    // OrtStatus を文字列にして解放する。成功（nullptr）なら空。
    std::string consume(OrtStatus* status) const
    {
        if (status == nullptr)
            return {};

        std::string message = api->GetErrorMessage(status);
        api->ReleaseStatus(status);
        return message.empty() ? "unknown error" : message;
    }
};

OnnxModel::OnnxModel(std::unique_ptr<Impl> i) : impl(std::move(i)) {}
OnnxModel::~OnnxModel() = default;

std::unique_ptr<OnnxModel> OnnxModel::load(const std::filesystem::path& dllPath,
                                           const std::filesystem::path& modelPath,
                                           int numThreads,
                                           std::string& error)
{
#if defined(_WIN32)
    if (! std::filesystem::exists(dllPath))
    {
        error = "onnxruntime.dll not found: " + dllPath.string();
        return nullptr;
    }

    if (! std::filesystem::exists(modelPath))
    {
        error = "model not found: " + modelPath.string();
        return nullptr;
    }

    // 絶対パスで読み込む。同じ DLL を一度読み込めば、以降は同じものが返る（解放しない）。
    HMODULE module = ::LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr)
    {
        error = "LoadLibrary failed for " + dllPath.string();
        return nullptr;
    }

    using GetApiBaseFn = const OrtApiBase*(ORT_API_CALL*)(void);
    const auto getApiBase = reinterpret_cast<GetApiBaseFn>(::GetProcAddress(module, "OrtGetApiBase"));
    if (getApiBase == nullptr)
    {
        error = "OrtGetApiBase not found";
        return nullptr;
    }

    // DLL が対応する最新の API バージョンから、ヘッダの版まで順に試す
    const OrtApi* api = nullptr;
    for (std::uint32_t version = ORT_API_VERSION; version >= 14 && api == nullptr; --version)
        api = getApiBase()->GetApi(version);

    if (api == nullptr)
    {
        error = "unsupported onnxruntime version";
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->api = api;

    if (const auto e = impl->consume(api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "koenote", &impl->env)); ! e.empty())
    {
        error = e;
        return nullptr;
    }

    if (const auto e = impl->consume(api->CreateSessionOptions(&impl->options)); ! e.empty())
    {
        error = e;
        return nullptr;
    }

    impl->consume(api->SetIntraOpNumThreads(impl->options, std::max(1, numThreads)));
    impl->consume(api->SetSessionGraphOptimizationLevel(impl->options, ORT_ENABLE_ALL));

    if (const auto e = impl->consume(api->CreateSession(impl->env, modelPath.c_str(), impl->options, &impl->session)); ! e.empty())
    {
        error = e;
        return nullptr;
    }

    if (const auto e = impl->consume(api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &impl->memory)); ! e.empty())
    {
        error = e;
        return nullptr;
    }

    std::unique_ptr<OnnxModel> model(new OnnxModel(std::move(impl)));

    OrtAllocator* allocator = nullptr;
    model->impl->consume(api->GetAllocatorWithDefaultOptions(&allocator));

    const auto collect = [&] (bool input, std::vector<std::string>& names)
    {
        std::size_t count = 0;
        const auto status = input ? api->SessionGetInputCount(model->impl->session, &count)
                                  : api->SessionGetOutputCount(model->impl->session, &count);
        model->impl->consume(status);

        for (std::size_t i = 0; i < count; ++i)
        {
            char* name = nullptr;
            const auto s = input ? api->SessionGetInputName(model->impl->session, i, allocator, &name)
                                 : api->SessionGetOutputName(model->impl->session, i, allocator, &name);

            if (model->impl->consume(s).empty() && name != nullptr)
            {
                names.emplace_back(name);
                allocator->Free(allocator, name);
            }
        }
    };

    collect(true, model->inputs);
    collect(false, model->outputs);

    if (model->inputs.empty() || model->outputs.empty())
    {
        error = "model has no inputs or outputs";
        return nullptr;
    }

    return model;
#else
    (void) dllPath;
    (void) modelPath;
    (void) numThreads;
    error = "ONNX Runtime loading is only supported on Windows";
    return nullptr;
#endif
}

bool OnnxModel::run(const std::vector<Input>& in, std::vector<float>& out, std::vector<std::int64_t>& outShape, std::string& error)
{
    const OrtApi* api = impl->api;
    std::vector<OrtValue*> values(in.size(), nullptr);
    std::vector<const char*> names;
    bool ok = true;

    const auto release = [&]
    {
        for (auto* v : values)
            if (v != nullptr)
                api->ReleaseValue(v);
    };

    for (std::size_t i = 0; i < in.size(); ++i)
    {
        std::int64_t count = 1;
        for (const auto d : in[i].shape)
            count *= d;

        const auto e = impl->consume(api->CreateTensorWithDataAsOrtValue(impl->memory,
                                                                          const_cast<float*>(in[i].data),
                                                                          static_cast<std::size_t>(count) * sizeof(float),
                                                                          in[i].shape.data(),
                                                                          in[i].shape.size(),
                                                                          ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                                                          &values[i]));
        if (! e.empty())
        {
            error = e;
            ok = false;
            break;
        }

        names.push_back(in[i].name.c_str());
    }

    if (! ok)
    {
        release();
        return false;
    }

    const char* outputName = outputs.front().c_str();
    OrtValue* result = nullptr;
    const auto e = impl->consume(api->Run(impl->session, nullptr, names.data(), values.data(), values.size(), &outputName, 1, &result));
    release();

    if (! e.empty())
    {
        error = e;
        return false;
    }

    float* data = nullptr;
    OrtTensorTypeAndShapeInfo* info = nullptr;
    std::size_t elements = 0;
    std::size_t rank = 0;

    bool good = impl->consume(api->GetTensorMutableData(result, reinterpret_cast<void**>(&data))).empty()
                && impl->consume(api->GetTensorTypeAndShape(result, &info)).empty()
                && impl->consume(api->GetTensorShapeElementCount(info, &elements)).empty()
                && impl->consume(api->GetDimensionsCount(info, &rank)).empty();

    if (good)
    {
        outShape.assign(rank, 0);
        good = impl->consume(api->GetDimensions(info, outShape.data(), rank)).empty();
    }

    if (good)
        out.assign(data, data + elements);
    else
        error = "failed to read the output tensor";

    if (info != nullptr)
        api->ReleaseTensorTypeAndShapeInfo(info);
    api->ReleaseValue(result);
    return good;
}
} // namespace koenote::neural
