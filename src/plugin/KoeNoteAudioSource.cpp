#include "plugin/KoeNoteAudioSource.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <new>

namespace koenote::plugin
{
// ホストのサンプルを読み込むスレッド。ARAAudioSourceReader は別スレッドからの読み込みを想定している。
class KoeNoteAudioSource::LoadThread final : public juce::Thread
{
public:
    LoadThread(KoeNoteAudioSource& ownerIn, juce::ARAAudioSourceReader& readerIn)
        : juce::Thread("KoeNote source loader"), owner(ownerIn), reader(readerIn)
    {
    }
    ~LoadThread() override { stopThread(10000); }

    void run() override
    {
        constexpr int kChunkFrames = 32768;

        const int numChannels = owner.getChannelCount();
        const auto numFrames = static_cast<std::int64_t>(owner.getSampleCount());
        const double sampleRate = owner.getSampleRate();

        if (numChannels <= 0 || numFrames <= 0 || sampleRate <= 0.0)
            return;

        auto result = std::make_shared<SourceAudioData>();

        try
        {
            result->numChannels = numChannels;
            result->numFrames = numFrames;
            result->sampleRate = sampleRate;
            result->channels.resize(static_cast<std::size_t>(numChannels));

            for (auto& channel : result->channels)
                channel.resize(static_cast<std::size_t>(numFrames));
        }
        catch (const std::bad_alloc&)
        {
            return;
        }

        std::vector<float*> destinations(static_cast<std::size_t>(numChannels));
        for (std::int64_t position = 0; position < numFrames; position += kChunkFrames)
        {
            if (threadShouldExit())
                return;

            const int count = static_cast<int>(std::min<std::int64_t>(kChunkFrames, numFrames - position));

            for (int c = 0; c < numChannels; ++c)
                destinations[static_cast<std::size_t>(c)] = result->channels[static_cast<std::size_t>(c)].data() + position;

            if (! reader.read(destinations.data(), numChannels, position, count))
                return; // サンプルアクセスが無効化された等。再度有効になった時点で読み直す。
        }

        if (threadShouldExit())
            return;

        for (const auto& channel : result->channels)
            result->channelPointers.push_back(channel.data());

        owner.publish(std::move(result));
    }

private:
    KoeNoteAudioSource& owner;
    juce::ARAAudioSourceReader& reader;
};

KoeNoteAudioSource::KoeNoteAudioSource(ARA::PlugIn::Document* document, ARA::ARAAudioSourceHostRef hostRef)
    : juce::ARAAudioSource(document, hostRef)
{
}

KoeNoteAudioSource::~KoeNoteAudioSource()
{
    stopLoading(); // 先にスレッドを止めてから、保留中の通知を取り消す
    cancelPendingUpdate();
}

void KoeNoteAudioSource::startLoading()
{
    stopLoading();

    if (! isSampleAccessEnabled())
        return;

    reader = std::make_unique<juce::ARAAudioSourceReader>(this);
    loader = std::make_unique<LoadThread>(*this, *reader);
    loader->startThread();
}

void KoeNoteAudioSource::stopLoading()
{
    if (loader != nullptr)
    {
        loader->stopThread(10000);
        loader.reset();
    }

    reader.reset();
}

std::shared_ptr<const SourceAudioData> KoeNoteAudioSource::sharedAudio() const
{
    const std::lock_guard<std::mutex> lock(dataMutex);
    return data;
}

void KoeNoteAudioSource::publish(std::shared_ptr<SourceAudioData> newData)
{
    std::shared_ptr<SourceAudioData> previous;

    {
        // 再読み込み時に、オーディオスレッドが旧データを参照していないことを保証してから差し替える
        render::ProcessingGate::WriteScope writeScope(*gate);
        const std::lock_guard<std::mutex> lock(dataMutex);
        previous = std::move(data);
        data = std::move(newData);
        published.store(data.get(), std::memory_order_release);
    }

    // previous はここで（ゲートの外で）解放される
    triggerAsyncUpdate();
}

void KoeNoteAudioSource::handleAsyncUpdate()
{
    if (onLoaded)
        onLoaded(*this);
}
} // namespace koenote::plugin
