#include "plugin/KoeNoteDocumentController.h"

#include "plugin/KoeNotePlaybackRenderer.h"

namespace koenote::plugin
{
namespace
{
// ARA アーカイブ内の形式バージョン。形式を変えるときは増やし、読み込み側に旧形式の処理を残す。
constexpr int kArchiveFormatVersion = 1;
} // namespace

void KoeNoteDocumentController::willBeginEditing(juce::ARADocument*)
{
    gate.beginWrite();
}

void KoeNoteDocumentController::didEndEditing(juce::ARADocument*)
{
    gate.endWrite();
}

void KoeNoteDocumentController::willEnableAudioSourceSamplesAccess(juce::ARAAudioSource* source, bool enable)
{
    // アクセスを無効にする前に、ホストの reader を使っている読み込みスレッドを止める
    if (! enable)
        static_cast<KoeNoteAudioSource*>(source)->stopLoading();
}

void KoeNoteDocumentController::didEnableAudioSourceSamplesAccess(juce::ARAAudioSource* source, bool enable)
{
    if (enable)
        static_cast<KoeNoteAudioSource*>(source)->startLoading();
}

void KoeNoteDocumentController::doUpdateAudioSourceContent(juce::ARAAudioSource* source, juce::ARAContentUpdateScopes scopes)
{
    // 音声が差し替えられた場合は読み直す
    if (scopes.affectSamples() && source->isSampleAccessEnabled())
        static_cast<KoeNoteAudioSource*>(source)->startLoading();
}

void KoeNoteDocumentController::willDestroyAudioSource(juce::ARAAudioSource* source)
{
    static_cast<KoeNoteAudioSource*>(source)->stopLoading();
}

juce::ARAAudioSource* KoeNoteDocumentController::doCreateAudioSource(juce::ARADocument* document,
                                                                     ARA::ARAAudioSourceHostRef hostRef) noexcept
{
    auto* source = new KoeNoteAudioSource(document, hostRef);
    source->setGate(&gate);
    return source;
}

juce::ARAPlaybackRenderer* KoeNoteDocumentController::doCreatePlaybackRenderer() noexcept
{
    return new KoeNotePlaybackRenderer(getDocumentController(), gate);
}

juce::ARAEditorRenderer* KoeNoteDocumentController::doCreateEditorRenderer() noexcept
{
    return new juce::ARAEditorRenderer(getDocumentController());
}

bool KoeNoteDocumentController::doRestoreObjectsFromStream(juce::ARAInputStream& input,
                                                           const juce::ARARestoreObjectsFilter*) noexcept
{
    const auto version = input.readInt();

    // 未対応の新しい形式は読み込まない
    if (input.failed() || version < 1 || version > kArchiveFormatVersion)
        return false;

    return ! input.failed();
}

bool KoeNoteDocumentController::doStoreObjectsToStream(juce::ARAOutputStream& output,
                                                       const juce::ARAStoreObjectsFilter*) noexcept
{
    return output.writeInt(kArchiveFormatVersion);
}
} // namespace koenote::plugin
