#include "model/EditModel.h"

#include "model/ByteStream.h"

#include <algorithm>
#include <cmath>

namespace koenote::model
{
namespace
{
constexpr double kEps = 1.0e-9;

bool finite(double v) noexcept { return std::isfinite(v); }

bool sameEdit(const NoteEdit& a, const NoteEdit& b) noexcept
{
    return a.pitchOffsetCents == b.pitchOffsetCents && a.centerCorrection == b.centerCorrection
           && a.vibratoScale == b.vibratoScale && a.driftScale == b.driftScale
           && a.transitionSmoothness == b.transitionSmoothness && a.targetStartSec == b.targetStartSec
           && a.targetEndSec == b.targetEndSec;
}

void writeNote(ByteWriter& w, const Note& n)
{
    w.writeU64(n.id);
    w.writeF64(n.startSec);
    w.writeF64(n.endSec);
    w.writeF32(n.detectedMidi);
    w.writeF32(n.level);
    w.writeF32(n.edit.pitchOffsetCents);
    w.writeF32(n.edit.centerCorrection);
    w.writeF32(n.edit.vibratoScale);
    w.writeF32(n.edit.driftScale);
    w.writeF32(n.edit.transitionSmoothness);
    w.writeF64(n.edit.targetStartSec);
    w.writeF64(n.edit.targetEndSec);
}

// 1 ノートあたりの保存サイズ（id 8 + 区間 16 + midi/level 8 + 編集 float 5 個 20 + 目標区間 16）
constexpr std::size_t kSerialisedNoteBytes = 8 + 16 + 8 + 20 + 16;

Note readNote(ByteReader& r)
{
    Note n;
    n.id = r.readU64();
    n.startSec = r.readF64();
    n.endSec = r.readF64();
    n.detectedMidi = r.readF32();
    n.level = r.readF32();
    n.edit.pitchOffsetCents = r.readF32();
    n.edit.centerCorrection = r.readF32();
    n.edit.vibratoScale = r.readF32();
    n.edit.driftScale = r.readF32();
    n.edit.transitionSmoothness = r.readF32();
    n.edit.targetStartSec = r.readF64();
    n.edit.targetEndSec = r.readF64();
    return n;
}
} // namespace

//==============================================================================
const Note* EditModel::findNote(NoteId id) const noexcept
{
    const auto i = indexOf(id);
    return i == npos ? nullptr : &noteList[i];
}

std::size_t EditModel::indexOf(NoteId id) const noexcept
{
    for (std::size_t i = 0; i < noteList.size(); ++i)
        if (noteList[i].id == id)
            return i;
    return npos;
}

NoteId EditModel::noteAt(double sourceSec) const noexcept
{
    for (const auto& n : noteList)
        if (sourceSec >= n.startSec && sourceSec < n.endSec)
            return n.id;
    return 0;
}

void EditModel::setAnalysis(PitchTrack newTrack, std::vector<Note> newNotes)
{
    std::sort(newNotes.begin(), newNotes.end(), [] (const Note& a, const Note& b) { return a.startSec < b.startSec; });

    track = std::move(newTrack);
    noteList = std::move(newNotes);
    nextNoteId = 1;
    for (auto& n : noteList)
        n.id = nextNoteId++;

    analysed = true;
    undoStack.clear();
    redoStack.clear();
    editDepth = 0;
    pendingBefore.reset();
    pendingChanged = false;
    ++revisionCounter;
}

//==============================================================================
bool EditModel::isValid(const std::vector<Note>& notes) noexcept
{
    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];

        if (! finite(n.startSec) || ! finite(n.endSec) || ! finite(n.detectedMidi) || ! finite(n.edit.pitchOffsetCents)
            || ! finite(n.edit.targetStartSec) || ! finite(n.edit.targetEndSec))
            return false;

        // 目標区間は開始・終了が揃って設定されているか、揃って未設定
        if ((n.edit.targetStartSec >= 0.0) != (n.edit.targetEndSec >= 0.0))
            return false;

        if (n.endSec - n.startSec < kMinNoteDurationSec - kEps)
            return false;

        if (n.outEndSec() - n.outStartSec() < kMinNoteDurationSec - kEps)
            return false;

        if (i > 0)
        {
            const auto& p = notes[i - 1];

            if (n.startSec < p.endSec - kEps || n.outStartSec() < p.outEndSec() - kEps)
                return false;
        }
    }

    return true;
}

void EditModel::restore(const Snapshot& s)
{
    noteList = s.notes;
    nextNoteId = s.nextId;
}

void EditModel::beginEdit()
{
    if (editDepth++ == 0)
    {
        pendingBefore = makeSnapshot();
        pendingChanged = false;
    }
}

void EditModel::endEdit()
{
    if (editDepth == 0)
        return;

    if (--editDepth == 0)
    {
        if (pendingChanged && pendingBefore.has_value())
        {
            undoStack.push_back(std::move(*pendingBefore));
            if (undoStack.size() > kMaxUndoDepth)
                undoStack.erase(undoStack.begin());
            redoStack.clear();
        }

        pendingBefore.reset();
        pendingChanged = false;
    }
}

void EditModel::commit(Snapshot before)
{
    ++revisionCounter;

    if (editDepth > 0)
    {
        pendingChanged = true;
        return;
    }

    undoStack.push_back(std::move(before));
    if (undoStack.size() > kMaxUndoDepth)
        undoStack.erase(undoStack.begin());
    redoStack.clear();
}

template <typename Fn>
bool EditModel::mutate(Fn&& mutator)
{
    auto before = makeSnapshot();

    if (! mutator() || ! isValid(noteList))
    {
        restore(before);
        return false;
    }

    // 内容が変わっていなければ履歴に積まない
    bool changed = before.notes.size() != noteList.size() || before.nextId != nextNoteId;

    for (std::size_t i = 0; i < noteList.size() && ! changed; ++i)
    {
        const auto& a = before.notes[i];
        const auto& b = noteList[i];
        changed = a.id != b.id || a.startSec != b.startSec || a.endSec != b.endSec
                  || a.detectedMidi != b.detectedMidi || ! sameEdit(a.edit, b.edit);
    }

    if (changed)
        commit(std::move(before));

    return true;
}

//==============================================================================
bool EditModel::setNotePitchOffset(NoteId id, float cents)
{
    const auto i = indexOf(id);
    if (i == npos)
        return false;

    return mutate([&] { noteList[i].edit.pitchOffsetCents = cents; return true; });
}

bool EditModel::setNoteEdit(NoteId id, const NoteEdit& edit)
{
    const auto i = indexOf(id);
    if (i == npos)
        return false;

    return mutate([&] { noteList[i].edit = edit; return true; });
}

bool EditModel::setNoteTiming(NoteId id, double targetStartSec, double targetEndSec)
{
    const auto i = indexOf(id);
    if (i == npos || targetStartSec < 0.0 || ! (targetEndSec > targetStartSec))
        return false;

    return mutate([&]
    {
        auto& n = noteList[i];

        // 元の区間と一致する場合はタイミング無編集に戻す
        if (std::abs(targetStartSec - n.startSec) < kEps && std::abs(targetEndSec - n.endSec) < kEps)
        {
            n.edit.targetStartSec = -1.0;
            n.edit.targetEndSec = -1.0;
        }
        else
        {
            n.edit.targetStartSec = targetStartSec;
            n.edit.targetEndSec = targetEndSec;
        }

        return true;
    });
}

bool EditModel::splitNote(NoteId id, double atSourceSec, NoteId* rightId)
{
    const auto i = indexOf(id);
    if (i == npos)
        return false;

    const Note src = noteList[i];

    if (atSourceSec < src.startSec + kMinNoteDurationSec || atSourceSec > src.endSec - kMinNoteDurationSec)
        return false;

    NoteId createdId = 0;

    const bool ok = mutate([&]
    {
        Note left = src;
        Note right = src;
        right.id = nextNoteId++;
        createdId = right.id;

        left.endSec = atSourceSec;
        right.startSec = atSourceSec;

        if (src.edit.hasTimingEdit())
        {
            // 出力時間軸上でも、元の区間内での位置と同じ比率で分割する
            const double ratio = (atSourceSec - src.startSec) / (src.endSec - src.startSec);
            const double mid = src.edit.targetStartSec + ratio * (src.edit.targetEndSec - src.edit.targetStartSec);
            left.edit.targetEndSec = mid;
            right.edit.targetStartSec = mid;
        }

        noteList[i] = left;
        noteList.insert(noteList.begin() + static_cast<std::ptrdiff_t>(i) + 1, right);
        return true;
    });

    if (ok && rightId != nullptr)
        *rightId = createdId;

    return ok;
}

bool EditModel::mergeWithNext(NoteId id)
{
    const auto i = indexOf(id);
    if (i == npos || i + 1 >= noteList.size())
        return false;

    return mutate([&]
    {
        const Note a = noteList[i];
        const Note b = noteList[i + 1];
        const double da = a.endSec - a.startSec;
        const double db = b.endSec - b.startSec;
        const double total = da + db;

        Note m = a;
        m.endSec = b.endSec;
        m.detectedMidi = static_cast<float>((a.detectedMidi * da + b.detectedMidi * db) / total);
        m.level = static_cast<float>((a.level * da + b.level * db) / total);

        // 目標ピッチは区間長で重み付けした平均に合わせる
        const double targetMidi = (a.targetMidi() * da + b.targetMidi() * db) / total;
        m.edit.pitchOffsetCents = static_cast<float>((targetMidi - m.detectedMidi) * 100.0);

        if (a.edit.hasTimingEdit() || b.edit.hasTimingEdit())
        {
            m.edit.targetStartSec = a.outStartSec();
            m.edit.targetEndSec = b.outEndSec();
        }

        noteList[i] = m;
        noteList.erase(noteList.begin() + static_cast<std::ptrdiff_t>(i) + 1);
        return true;
    });
}

bool EditModel::moveBoundary(NoteId id, double newBoundarySec)
{
    const auto i = indexOf(id);
    if (i == npos || i + 1 >= noteList.size() || ! finite(newBoundarySec))
        return false;

    return mutate([&]
    {
        auto& a = noteList[i];
        auto& b = noteList[i + 1];
        a.endSec = newBoundarySec;
        b.startSec = newBoundarySec;
        return true;
    });
}

bool EditModel::resetNote(NoteId id)
{
    const auto i = indexOf(id);
    if (i == npos)
        return false;

    return mutate([&] { noteList[i].edit = NoteEdit {}; return true; });
}

bool EditModel::resetAll()
{
    return mutate([&]
    {
        for (auto& n : noteList)
            n.edit = NoteEdit {};
        return true;
    });
}

bool EditModel::undo()
{
    if (undoStack.empty())
        return false;

    redoStack.push_back(makeSnapshot());
    restore(undoStack.back());
    undoStack.pop_back();
    ++revisionCounter;
    return true;
}

bool EditModel::redo()
{
    if (redoStack.empty())
        return false;

    undoStack.push_back(makeSnapshot());
    restore(redoStack.back());
    redoStack.pop_back();
    ++revisionCounter;
    return true;
}

//==============================================================================
std::vector<std::uint8_t> EditModel::serialize() const
{
    ByteWriter w;
    w.writeU32(kEditModelMagic);
    w.writeU32(kEditModelFormatVersion);
    w.writeU8(analysed ? 1 : 0);
    w.writeU64(nextNoteId);

    w.writeF64(track.startSec);
    w.writeF64(track.hopSec);
    w.writeU64(track.f0Hz.size());
    for (const auto v : track.f0Hz)
        w.writeF32(v);
    for (std::size_t i = 0; i < track.f0Hz.size(); ++i)
        w.writeF32(i < track.voicing.size() ? track.voicing[i] : 0.0f);

    w.writeU64(noteList.size());
    for (const auto& n : noteList)
        writeNote(w, n);

    return w.release();
}

std::optional<EditModel> EditModel::deserialize(const std::uint8_t* data, std::size_t size)
{
    ByteReader r(data, size);

    if (r.readU32() != kEditModelMagic)
        return std::nullopt;

    const auto version = r.readU32();

    // 新しいバージョンで保存されたデータは読めない。旧バージョンは将来ここで分岐して読み込む。
    if (r.failed() || version == 0 || version > kEditModelFormatVersion)
        return std::nullopt;

    EditModel m;
    m.analysed = r.readU8() != 0;
    m.nextNoteId = r.readU64();

    m.track.startSec = r.readF64();
    m.track.hopSec = r.readF64();

    const auto frames = r.readU64();
    if (! r.plausibleCount(frames, 8))
        return std::nullopt;

    m.track.f0Hz.resize(static_cast<std::size_t>(frames));
    m.track.voicing.resize(static_cast<std::size_t>(frames));
    for (auto& v : m.track.f0Hz)
        v = r.readF32();
    for (auto& v : m.track.voicing)
        v = r.readF32();

    const auto count = r.readU64();
    if (! r.plausibleCount(count, kSerialisedNoteBytes))
        return std::nullopt;

    m.noteList.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i)
        m.noteList.push_back(readNote(r));

    if (r.failed() || ! (m.track.hopSec > 0.0) || ! isValid(m.noteList))
        return std::nullopt;

    // nextNoteId が既存 id と衝突しないことを保証する
    for (const auto& n : m.noteList)
        m.nextNoteId = std::max(m.nextNoteId, n.id + 1);

    m.revisionCounter = 1;
    return m;
}
} // namespace koenote::model
