#pragma once

#include "plugin/KoeNoteAudioSource.h"
#include "render/ProcessingGate.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace koenote::plugin
{
// ARA の DocumentController（要件 5章「プラグイン層」）。
// モデル編集中はゲートを閉じ、オーディオスレッドが編集途中のモデルに触れないようにする。
class KoeNoteDocumentController final : public juce::ARADocumentControllerSpecialisation
{
public:
    using juce::ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

    render::ProcessingGate& getGate() noexcept { return gate; }

protected:
    void willBeginEditing(juce::ARADocument*) override;
    void didEndEditing(juce::ARADocument*) override;

    void willEnableAudioSourceSamplesAccess(juce::ARAAudioSource*, bool enable) override;
    void didEnableAudioSourceSamplesAccess(juce::ARAAudioSource*, bool enable) override;
    void doUpdateAudioSourceContent(juce::ARAAudioSource*, juce::ARAContentUpdateScopes) override;
    void willDestroyAudioSource(juce::ARAAudioSource*) override;

    juce::ARAAudioSource* doCreateAudioSource(juce::ARADocument*, ARA::ARAAudioSourceHostRef) noexcept override;
    juce::ARAAudioModification* doCreateAudioModification(juce::ARAAudioSource*,
                                                          ARA::ARAAudioModificationHostRef,
                                                          const juce::ARAAudioModification*) noexcept override;
    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override;
    juce::ARAEditorRenderer* doCreateEditorRenderer() noexcept override;

    bool doRestoreObjectsFromStream(juce::ARAInputStream&, const juce::ARARestoreObjectsFilter*) noexcept override;
    bool doStoreObjectsToStream(juce::ARAOutputStream&, const juce::ARAStoreObjectsFilter*) noexcept override;

private:
    render::ProcessingGate gate;
};
} // namespace koenote::plugin
