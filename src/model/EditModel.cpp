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
           && a.targetEndSec == b.targetEndSec && a.gainDb == b.gainDb && a.formantShiftCents == b.formantShiftCents
           && a.formantFollow == b.formantFollow && a.consonantGainDb == b.consonantGainDb
           && a.breathGainDb == b.breathGainDb && a.attackHoldSec == b.attackHoldSec
           && a.releaseHoldSec == b.releaseHoldSec && a.sibilanceGainDb == b.sibilanceGainDb;
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
    // v2 で追加
    w.writeF32(n.edit.gainDb);
    w.writeF32(n.edit.formantShiftCents);
    w.writeF32(n.edit.formantFollow);
    w.writeF32(n.edit.consonantGainDb);
    w.writeF32(n.edit.breathGainDb);
    // v4 で追加
    w.writeF32(n.edit.attackHoldSec);
    w.writeF32(n.edit.releaseHoldSec);
    w.writeF32(n.edit.sibilanceGainDb);
}

// 1 ノートあたりの保存サイズ（id 8 + 区間 16 + midi/level 8 + 編集 float 5 個 20 + 目標区間 16）。v2 は float 5 個が加わる。
constexpr std::size_t kSerialisedNoteBytesV1 = 8 + 16 + 8 + 20 + 16;
constexpr std::size_t kSerialisedNoteBytesV2 = kSerialisedNoteBytesV1 + 20;
constexpr std::size_t kSerialisedNoteBytesV4 = kSerialisedNoteBytesV2 + 12;

Note readNote(ByteReader& r, std::uint32_t version)
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

    if (version >= 2)
    {
        n.edit.gainDb = r.readF32();
        n.edit.formantShiftCents = r.readF32();
        n.edit.formantFollow = r.readF32();
        n.edit.consonantGainDb = r.readF32();
        n.edit.breathGainDb = r.readF32();
    }

    if (version >= 4)
    {
        n.edit.attackHoldSec = r.readF32();
        n.edit.releaseHoldSec = r.readF32();
        n.edit.sibilanceGainDb = r.readF32();
    }

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

    targetList.clear();
    assignmentList.clear();
    pitchDrawing.clear();

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
            || ! finite(n.edit.targetStartSec) || ! finite(n.edit.targetEndSec) || ! finite(n.edit.gainDb)
            || ! finite(n.edit.formantShiftCents) || ! finite(n.edit.formantFollow) || ! finite(n.edit.consonantGainDb)
            || ! finite(n.edit.breathGainDb) || ! finite(n.edit.centerCorrection) || ! finite(n.edit.vibratoScale)
            || ! finite(n.edit.driftScale) || ! finite(n.edit.transitionSmoothness) || ! finite(n.edit.sibilanceGainDb))
            return false;

        if (! (n.edit.attackHoldSec >= 0.0f && n.edit.attackHoldSec <= NoteEdit::kMaxHoldSec)
            || ! (n.edit.releaseHoldSec >= 0.0f && n.edit.releaseHoldSec <= NoteEdit::kMaxHoldSec))
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
    targetList = s.targets;
    assignmentList = s.assignments;
    pitchDrawing = s.drawing;
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
    bool changed = before.notes.size() != noteList.size() || before.nextId != nextNoteId
                   || before.assignments != assignmentList || before.targets.size() != targetList.size()
                   || ! (before.drawing == pitchDrawing);

    for (std::size_t i = 0; i < targetList.size() && ! changed; ++i)
    {
        const auto& a = before.targets[i];
        const auto& b = targetList[i];
        changed = a.startSec != b.startSec || a.endSec != b.endSec || a.midi != b.midi;
    }

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

        // 子音は前半の直前、ブレスは後半の直後にだけ付く
        left.edit.breathGainDb = 0.0f;
        right.edit.consonantGainDb = 0.0f;

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
        m.edit.breathGainDb = b.edit.breathGainDb;

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
        pitchDrawing.clear();
        return true;
    });
}

bool EditModel::drawPitch(std::size_t firstFrame, const std::vector<float>& midi)
{
    if (firstFrame >= track.size() || midi.empty())
        return false;

    const auto count = std::min(midi.size(), track.size() - firstFrame);
    const std::vector<float> values(midi.begin(), midi.begin() + static_cast<std::ptrdiff_t>(count));

    if (! std::all_of(values.begin(), values.end(), [] (float v) { return std::isfinite(v) && v > 0.0f && v < 140.0f; }))
        return false;

    return mutate([&] { pitchDrawing.draw(static_cast<std::uint32_t>(firstFrame), values); return true; });
}

bool EditModel::erasePitch(std::size_t firstFrame, std::size_t endFrame)
{
    if (endFrame <= firstFrame)
        return false;

    const auto clampFrame = [&] (std::size_t f) { return static_cast<std::uint32_t>(std::min<std::size_t>(f, 0xFFFFFFFFu)); };
    return mutate([&] { pitchDrawing.erase(clampFrame(firstFrame), clampFrame(endFrame)); return true; });
}

bool EditModel::clearDrawing()
{
    return mutate([&] { pitchDrawing.clear(); return true; });
}

bool EditModel::applyEdits(const std::vector<std::pair<NoteId, NoteEdit>>& edits)
{
    if (edits.empty())
        return false;

    return mutate([&]
    {
        for (const auto& [id, edit] : edits)
        {
            const auto i = indexOf(id);
            if (i == npos)
                return false;
            noteList[i].edit = edit;
        }

        // タイミングを前から順に整える（重なりは、ノートの長さを保ったまま後ろへ押し出して解消する）
        double previousEnd = -1.0e18;

        for (auto& n : noteList)
        {
            double s = n.outStartSec();
            double e = n.outEndSec();

            if (s < previousEnd)
            {
                // 重なる場合は、長さを保ったまま後ろへ押し出す
                e += previousEnd - s;
                s = previousEnd;
            }
            if (e - s < kMinNoteDurationSec)
                e = s + kMinNoteDurationSec;

            if (std::abs(s - n.startSec) < kEps && std::abs(e - n.endSec) < kEps)
            {
                n.edit.targetStartSec = -1.0;
                n.edit.targetEndSec = -1.0;
            }
            else
            {
                n.edit.targetStartSec = std::max(0.0, s);
                n.edit.targetEndSec = std::max(0.0, s) + (e - s);
            }

            previousEnd = n.outEndSec();
        }

        return true;
    });
}

int EditModel::assignmentOf(NoteId id) const noexcept
{
    for (const auto& a : assignmentList)
        if (a.first == id)
            return a.second;
    return -1;
}

bool EditModel::setTargets(std::vector<TargetNote> newTargets, std::vector<std::pair<NoteId, int>> newAssignments)
{
    for (const auto& t : newTargets)
        if (! finite(t.startSec) || ! finite(t.endSec) || ! finite(t.midi))
            return false;

    // 割り当ては、存在するノートと目標の範囲内で、目標が重複しないものだけを残す
    std::vector<std::pair<NoteId, int>> cleaned;
    std::vector<char> used(newTargets.size(), 0);

    for (const auto& a : newAssignments)
    {
        if (findNote(a.first) == nullptr || a.second < 0 || static_cast<std::size_t>(a.second) >= newTargets.size()
            || used[static_cast<std::size_t>(a.second)])
            continue;

        used[static_cast<std::size_t>(a.second)] = 1;
        cleaned.push_back(a);
    }

    return mutate([&]
    {
        targetList = std::move(newTargets);
        assignmentList = std::move(cleaned);
        return true;
    });
}

bool EditModel::assignTarget(NoteId id, int targetIndex)
{
    if (findNote(id) == nullptr || targetIndex < -1 || targetIndex >= static_cast<int>(targetList.size()))
        return false;

    return mutate([&]
    {
        std::erase_if(assignmentList, [&] (const auto& a) { return a.first == id || (targetIndex >= 0 && a.second == targetIndex); });

        if (targetIndex >= 0)
            assignmentList.emplace_back(id, targetIndex);

        return true;
    });
}

bool EditModel::clearTargets()
{
    return mutate([&]
    {
        targetList.clear();
        assignmentList.clear();
        return true;
    });
}

void EditModel::setSettings(const ModelSettings& s)
{
    if (s.renderMode == modelSettings.renderMode && s.snapMode == modelSettings.snapMode && s.keyRoot == modelSettings.keyRoot
        && s.keyMinor == modelSettings.keyMinor && s.gridDivision == modelSettings.gridDivision && s.manualBpm == modelSettings.manualBpm
        && s.timingRangeActive == modelSettings.timingRangeActive && s.timingRangeStartSec == modelSettings.timingRangeStartSec
        && s.timingRangeEndSec == modelSettings.timingRangeEndSec && s.deviationThresholdCents == modelSettings.deviationThresholdCents)
        return;

    modelSettings = s;
    ++revisionCounter;
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

    // v2: MIDI の目標ノート、割り当て、設定
    w.writeU64(targetList.size());
    for (const auto& t : targetList)
    {
        w.writeF64(t.startSec);
        w.writeF64(t.endSec);
        w.writeF32(t.midi);
    }

    w.writeU64(assignmentList.size());
    for (const auto& a : assignmentList)
    {
        w.writeU64(a.first);
        w.writeU32(static_cast<std::uint32_t>(a.second));
    }

    w.writeU8(modelSettings.renderMode);
    w.writeU8(modelSettings.snapMode);
    w.writeU8(modelSettings.keyRoot);
    w.writeU8(modelSettings.keyMinor);
    w.writeU8(modelSettings.gridDivision);
    w.writeF32(modelSettings.manualBpm);
    w.writeU8(modelSettings.timingRangeActive ? 1 : 0);
    w.writeF64(modelSettings.timingRangeStartSec);
    w.writeF64(modelSettings.timingRangeEndSec);

    // v4: ずれの表示のしきい値、手描きのピッチ曲線
    w.writeU8(modelSettings.deviationThresholdCents);
    w.writeU64(pitchDrawing.segments().size());
    for (const auto& s : pitchDrawing.segments())
    {
        w.writeU32(s.startFrame);
        w.writeU32(static_cast<std::uint32_t>(s.midi.size()));
        for (const auto v : s.midi)
            w.writeF32(v);
    }

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
    if (! r.plausibleCount(count, version >= 4 ? kSerialisedNoteBytesV4 : (version >= 2 ? kSerialisedNoteBytesV2 : kSerialisedNoteBytesV1)))
        return std::nullopt;

    m.noteList.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i)
        m.noteList.push_back(readNote(r, version));

    if (version >= 2)
    {
        const auto targetCount = r.readU64();
        if (! r.plausibleCount(targetCount, 20))
            return std::nullopt;

        m.targetList.reserve(static_cast<std::size_t>(targetCount));
        for (std::uint64_t i = 0; i < targetCount; ++i)
        {
            TargetNote t;
            t.startSec = r.readF64();
            t.endSec = r.readF64();
            t.midi = r.readF32();
            m.targetList.push_back(t);
        }

        const auto assignmentCount = r.readU64();
        if (! r.plausibleCount(assignmentCount, 12))
            return std::nullopt;

        for (std::uint64_t i = 0; i < assignmentCount; ++i)
        {
            const auto id = r.readU64();
            const auto index = static_cast<std::int32_t>(r.readU32());

            // 範囲外の割り当ては捨てる
            if (index >= 0 && static_cast<std::size_t>(index) < m.targetList.size())
                m.assignmentList.emplace_back(id, index);
        }

        m.modelSettings.renderMode = r.readU8();
        m.modelSettings.snapMode = r.readU8();
        m.modelSettings.keyRoot = r.readU8();
        m.modelSettings.keyMinor = r.readU8();
        m.modelSettings.gridDivision = r.readU8();
        m.modelSettings.manualBpm = r.readF32();

        if (version >= 3)
        {
            m.modelSettings.timingRangeActive = r.readU8() != 0;
            m.modelSettings.timingRangeStartSec = r.readF64();
            m.modelSettings.timingRangeEndSec = r.readF64();
        }

        if (version >= 4)
        {
            m.modelSettings.deviationThresholdCents = r.readU8();

            const auto segmentCount = r.readU64();
            if (! r.plausibleCount(segmentCount, 12))
                return std::nullopt;

            std::vector<PitchDrawing::Segment> segments;
            segments.reserve(static_cast<std::size_t>(segmentCount));

            for (std::uint64_t i = 0; i < segmentCount; ++i)
            {
                PitchDrawing::Segment s;
                s.startFrame = r.readU32();
                const auto length = r.readU32();

                if (r.failed() || ! r.plausibleCount(length, 4))
                    return std::nullopt;

                s.midi.resize(length);
                for (auto& v : s.midi)
                    v = r.readF32();

                // ピッチ曲線の範囲を超える区間は不正
                if (static_cast<std::uint64_t>(s.startFrame) + length > m.track.f0Hz.size())
                    return std::nullopt;

                segments.push_back(std::move(s));
            }

            if (! PitchDrawing::fromSegments(std::move(segments), m.pitchDrawing))
                return std::nullopt;
        }

        // 設定値の範囲を検査する（壊れたデータで UI が不正な状態にならないように）
        auto& s = m.modelSettings;
        if (s.renderMode > 1 || s.snapMode > 2 || s.keyRoot > 11 || s.keyMinor > 1 || s.gridDivision > 3 || ! std::isfinite(s.manualBpm)
            || s.manualBpm < 0.0f || s.manualBpm > 999.0f || ! std::isfinite(s.timingRangeStartSec) || ! std::isfinite(s.timingRangeEndSec)
            || s.deviationThresholdCents > ModelSettings::kMaxDeviationThresholdCents
            || (s.timingRangeActive && ! (s.timingRangeStartSec >= 0.0 && s.timingRangeEndSec > s.timingRangeStartSec)))
            s = ModelSettings {};
    }

    if (r.failed() || ! (m.track.hopSec > 0.0) || ! isValid(m.noteList))
        return std::nullopt;

    // nextNoteId が既存 id と衝突しないことを保証する
    for (const auto& n : m.noteList)
        m.nextNoteId = std::max(m.nextNoteId, n.id + 1);

    m.revisionCounter = 1;
    return m;
}
} // namespace koenote::model
