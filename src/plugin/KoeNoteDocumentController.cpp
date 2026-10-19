#include "plugin/KoeNoteDocumentController.h"

#include "plugin/KoeNoteAudioModification.h"
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

    // 読み込みが終わったら、この音声を使う全ての AudioModification で解析・レンダリングを始める
    source->onLoaded = [] (KoeNoteAudioSource& s)
    {
        for (auto* modification : s.getAudioModifications<KoeNoteAudioModification>())
            modification->onSourceAudioReady();
    };

    return source;
}

juce::ARAAudioModification* KoeNoteDocumentController::doCreateAudioModification(
    juce::ARAAudioSource* source,
    ARA::ARAAudioModificationHostRef hostRef,
    const juce::ARAAudioModification* modificationToClone) noexcept
{
    return new KoeNoteAudioModification(source, hostRef, modificationToClone, &gate);
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
                                                           const juce::ARARestoreObjectsFilter* filter) noexcept
{
    const auto version = input.readInt();

    // 未対応の新しい形式は読み込まない
    if (input.failed() || version < 1 || version > kArchiveFormatVersion)
        return false;

    const auto count = input.readInt64();

    for (juce::int64 i = 0; i < count && ! input.failed(); ++i)
    {
        const auto persistentId = input.readString();
        const auto size = input.readInt64();

        // 壊れたデータで巨大な確保をしないよう、サイズを検査する
        if (input.failed() || size < 0 || size > (static_cast<juce::int64>(1) << 31))
            return false;

        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if (size > 0 && input.read(bytes.data(), static_cast<int>(size)) != static_cast<int>(size))
            return false;

        // このプロジェクトに存在しない AudioModification の状態は捨てる
        auto* modification = filter->getAudioModificationToRestoreStateWithID<KoeNoteAudioModification>(persistentId.getCharPointer());
        if (modification == nullptr)
            continue;

        // 復元できないモデル（新しすぎるバージョン等）は無視して、既定の状態のまま続ける
        if (auto restored = model::EditModel::deserialize(bytes.data(), bytes.size()))
            modification->replaceModel(std::move(*restored));
    }

    return ! input.failed();
}

bool KoeNoteDocumentController::doStoreObjectsToStream(juce::ARAOutputStream& output,
                                                       const juce::ARAStoreObjectsFilter* filter) noexcept
{
    const auto& modifications = filter->getAudioModificationsToStore<KoeNoteAudioModification>();

    if (! output.writeInt(kArchiveFormatVersion) || ! output.writeInt64(static_cast<juce::int64>(modifications.size())))
        return false;

    for (auto* modification : modifications)
    {
        const auto bytes = modification->getModel().serialize();

        if (! output.writeString(modification->getPersistentID())
            || ! output.writeInt64(static_cast<juce::int64>(bytes.size()))
            || ! output.write(bytes.data(), bytes.size()))
            return false;
    }

    return true;
}
} // namespace koenote::plugin
