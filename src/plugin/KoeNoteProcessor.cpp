#include "plugin/KoeNoteProcessor.h"

#include "ui/KoeNoteEditor.h"

namespace koenote::plugin
{
KoeNoteProcessor::KoeNoteProcessor() : juce::AudioProcessor(makeBusesProperties()) {}

juce::AudioProcessor::BusesProperties KoeNoteProcessor::makeBusesProperties()
{
    return BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true);
}

void KoeNoteProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    prepareToPlayForARA(sampleRate, samplesPerBlock, getMainBusNumOutputChannels(), getProcessingPrecision());
}

void KoeNoteProcessor::releaseResources()
{
    releaseResourcesForARA();
}

bool KoeNoteProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // 入力は出力と同じ構成、または無効のみ許可する
    return in == out || in.isDisabled();
}

void KoeNoteProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Editor に再生位置を渡す（atomic の書き込みのみ）
    if (auto* host = getPlayHead())
    {
        const auto position = host->getPosition();
        playheadPlaying.store(position.hasValue() && position->getIsPlaying(), std::memory_order_relaxed);
        playheadSeconds.store(position.hasValue() ? position->getTimeInSeconds().orFallback(0.0) : 0.0, std::memory_order_relaxed);
    }

    // ARA の再生レンダラーが処理できない場合（ARA 非対応のホスト等）は入力をそのまま返す
    if (! processBlockForARA(buffer, isRealtime(), getPlayHead()))
        processBlockBypassed(buffer, midi);
}

double KoeNoteProcessor::getTailLengthSeconds() const
{
    double tail = 0.0;
    return getTailLengthSecondsForARA(tail) ? tail : 0.0;
}

juce::AudioProcessorEditor* KoeNoteProcessor::createEditor()
{
    return new ui::KoeNoteEditor(*this);
}
} // namespace koenote::plugin
