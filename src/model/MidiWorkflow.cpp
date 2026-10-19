#include "model/MidiWorkflow.h"

#include "analysis/MidiMatcher.h"

#include <algorithm>
#include <cmath>

namespace koenote::model
{
namespace
{
int velocityOf(float level) noexcept
{
    const double db = 20.0 * std::log10(std::max(1.0e-6, static_cast<double>(level)));
    const double t = std::clamp((db + 48.0) / 42.0, 0.0, 1.0); // -48 dB → 20、-6 dB 以上 → 127
    return static_cast<int>(std::lround(20.0 + t * 107.0));
}

double toMidi(double hz) noexcept { return 69.0 + 12.0 * std::log2(hz / 440.0); }
} // namespace

std::vector<midi::MidiNote> notesToMidi(const std::vector<Note>& notes, bool edited)
{
    std::vector<midi::MidiNote> result;
    result.reserve(notes.size());

    for (const auto& n : notes)
    {
        midi::MidiNote m;
        m.startSec = edited ? n.outStartSec() : n.startSec;
        m.endSec = edited ? n.outEndSec() : n.endSec;
        m.pitch = std::clamp(static_cast<int>(std::lround(edited ? n.targetMidi() : n.detectedMidi)), 0, 127);
        m.velocity = velocityOf(n.level);
        result.push_back(m);
    }

    return result;
}

std::vector<midi::PitchBendPoint> pitchBendCurve(const PitchTrack& track, const std::vector<Note>& notes)
{
    std::vector<midi::PitchBendPoint> curve;

    for (const auto& n : notes)
    {
        const double centre = std::round(static_cast<double>(n.detectedMidi));

        for (std::size_t f = 0; f < track.size(); ++f)
        {
            const double t = track.timeOf(f);
            if (t < n.startSec)
                continue;
            if (t >= n.endSec)
                break;

            if (track.isVoiced(f))
                curve.push_back({ t, toMidi(static_cast<double>(track.f0Hz[f])) - centre });
        }
    }

    return curve;
}

double autoMidiOffset(const std::vector<Note>& notes, const midi::MidiSequence& sequence)
{
    if (notes.empty() || sequence.notes.empty())
        return 0.0;

    return sequence.notes.front().startSec - notes.front().startSec;
}

MidiImport importMidi(const std::vector<Note>& notes, const midi::MidiSequence& sequence, double offsetSec)
{
    MidiImport result;

    std::vector<analysis::TimedNote> analysed;
    analysed.reserve(notes.size());
    for (const auto& n : notes)
        analysed.push_back({ n.startSec, n.endSec, n.detectedMidi });

    std::vector<analysis::TimedNote> target;
    target.reserve(sequence.notes.size());
    for (const auto& m : sequence.notes)
    {
        TargetNote t;
        t.startSec = std::max(0.0, m.startSec - offsetSec);
        t.endSec = std::max(t.startSec + 0.001, m.endSec - offsetSec);
        t.midi = static_cast<float>(m.pitch);
        result.targets.push_back(t);
        target.push_back({ t.startSec, t.endSec, t.midi });
    }

    const auto match = analysis::matchNotes(analysed, target);

    for (const auto& [a, t] : match.pairs)
        result.assignments.emplace_back(notes[static_cast<std::size_t>(a)].id, t);

    result.unmatchedAnalysis = static_cast<int>(match.unmatchedAnalysis.size());
    result.unmatchedTargets = static_cast<int>(match.unmatchedTarget.size());
    return result;
}

std::vector<std::pair<NoteId, NoteEdit>> computeTargetEdits(const EditModel& model, float pitchStrength, float timingStrength)
{
    std::vector<std::pair<NoteId, NoteEdit>> edits;
    const double p = std::clamp(static_cast<double>(pitchStrength), 0.0, 1.0);
    const double s = std::clamp(static_cast<double>(timingStrength), 0.0, 1.0);

    for (const auto& note : model.notes())
    {
        const int index = model.assignmentOf(note.id);
        if (index < 0 || static_cast<std::size_t>(index) >= model.targets().size())
            continue;

        const auto& target = model.targets()[static_cast<std::size_t>(index)];
        NoteEdit edit = note.edit;

        edit.pitchOffsetCents = static_cast<float>((static_cast<double>(target.midi) - static_cast<double>(note.detectedMidi)) * 100.0 * p);
        edit.centerCorrection = std::max(edit.centerCorrection, static_cast<float>(p));

        if (s > 0.0)
        {
            const double start = note.startSec + (target.startSec - note.startSec) * s;
            const double end = std::max(start + EditModel::kMinNoteDurationSec, note.endSec + (target.endSec - note.endSec) * s);
            edit.targetStartSec = std::max(0.0, start);
            edit.targetEndSec = std::max(0.0, start) + (end - start);
        }
        else
        {
            edit.targetStartSec = -1.0;
            edit.targetEndSec = -1.0;
        }

        edits.emplace_back(note.id, edit);
    }

    return edits;
}
} // namespace koenote::model
