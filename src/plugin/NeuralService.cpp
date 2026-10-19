#include "plugin/NeuralService.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstdlib>
#include <thread>

namespace koenote::plugin
{
namespace
{
void addWithSubdirectories(std::vector<std::filesystem::path>& out, const juce::File& dir)
{
    if (! dir.isDirectory())
        return;

    out.emplace_back(dir.getFullPathName().toWideCharPointer());

    for (const auto& child : dir.findChildFiles(juce::File::findDirectories, false))
        out.emplace_back(child.getFullPathName().toWideCharPointer());
}

std::vector<std::filesystem::path> candidateDirectories()
{
    std::vector<std::filesystem::path> dirs;

    addWithSubdirectories(dirs, juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("KoeNote").getChildFile("neural"));
    addWithSubdirectories(dirs, juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getChildFile("neural"));

    if (const auto env = juce::SystemStats::getEnvironmentVariable("KOENOTE_NEURAL_DIR", {}); env.isNotEmpty())
        addWithSubdirectories(dirs, juce::File(env));

    return dirs;
}
} // namespace

NeuralService& NeuralService::get()
{
    static NeuralService instance;
    return instance;
}

neural::VocoderPackage NeuralService::package()
{
    const std::lock_guard<std::mutex> lock(mutex);
    const auto now = std::chrono::steady_clock::now();

    if (! haveCache || now - cachedAt > std::chrono::seconds(5))
    {
        cachedPackage = neural::findVocoderPackage(candidateDirectories());
        cachedAt = now;
        haveCache = true;
    }

    return cachedPackage;
}

void NeuralService::rescan()
{
    const std::lock_guard<std::mutex> lock(mutex);
    haveCache = false;
}

std::shared_ptr<neural::NsfHifiGanVocoder> NeuralService::vocoder(std::string& error)
{
    const auto pkg = package();

    if (! pkg.available)
    {
        error = pkg.reason;
        return nullptr;
    }

    const std::lock_guard<std::mutex> lock(loadMutex);

    // 同じモデルは読み込み済みのものを再利用する。失敗は同じモデルでは繰り返さない。
    const auto key = pkg.modelFile.string();

    if (key == loadedModel)
    {
        error = loadError;
        return loaded;
    }

    const int threads = static_cast<int>(std::clamp(std::thread::hardware_concurrency() / 2u, 1u, 6u));
    std::string loadMessage;
    auto created = neural::NsfHifiGanVocoder::create(pkg, threads, loadMessage);

    loadedModel = key;
    loadError = loadMessage;
    loaded = std::shared_ptr<neural::NsfHifiGanVocoder>(std::move(created));
    error = loadError;
    return loaded;
}
} // namespace koenote::plugin
