#include "model/MixCorrection.h"

#include <algorithm>
#include <cmath>

namespace koenote::model
{
namespace
{
bool selected(const std::vector<NoteId>& ids, NoteId id)
{
    return ids.empty() || std::find(ids.begin(), ids.end(), id) != ids.end();
}

double levelDb(float level) noexcept
{
    return 20.0 * std::log10(static_cast<double>(level));
}
} // namespace

std::vector<std::pair<NoteId, NoteEdit>> computeLevelingEdits(const std::vector<Note>& notes,
                                                              const std::vector<NoteId>& ids,
                                                              float strength,
                                                              float maxDb)
{
    std::vector<std::pair<NoteId, NoteEdit>> edits;
    const double s = std::clamp(static_cast<double>(strength), 0.0, 1.0);
    const double limit = std::max(0.0, static_cast<double>(maxDb));

    // 対象ノートの音量の dB 平均（ノートの長さで重み付け）
    double sum = 0.0;
    double weight = 0.0;

    for (const auto& n : notes)
    {
        if (! selected(ids, n.id) || ! (n.level > 0.0f))
            continue;

        const double duration = std::max(0.0, n.endSec - n.startSec);
        sum += levelDb(n.level) * duration;
        weight += duration;
    }

    if (! (weight > 0.0))
        return edits;

    const double meanDb = sum / weight;

    for (const auto& n : notes)
    {
        if (! selected(ids, n.id) || ! (n.level > 0.0f))
            continue;

        NoteEdit edit = n.edit;
        edit.gainDb = static_cast<float>(std::clamp((meanDb - levelDb(n.level)) * s, -limit, limit));
        edits.emplace_back(n.id, edit);
    }

    return edits;
}

std::vector<std::pair<NoteId, NoteEdit>> computeGainEdits(const std::vector<Note>& notes,
                                                          const std::vector<NoteId>& ids,
                                                          GainTarget target,
                                                          float db)
{
    std::vector<std::pair<NoteId, NoteEdit>> edits;

    if (! std::isfinite(db))
        return edits;

    for (const auto& n : notes)
    {
        if (! selected(ids, n.id))
            continue;

        NoteEdit edit = n.edit;

        switch (target)
        {
            case GainTarget::Note:      edit.gainDb = db; break;
            case GainTarget::Consonant: edit.consonantGainDb = db; break;
            case GainTarget::Breath:    edit.breathGainDb = db; break;
            case GainTarget::Sibilance: edit.sibilanceGainDb = db; break;
        }

        edits.emplace_back(n.id, edit);
    }

    return edits;
}

NoteEdit pasteEditParameters(const NoteEdit& dst, const NoteEdit& src) noexcept
{
    NoteEdit result = dst;
    result.centerCorrection = src.centerCorrection;
    result.vibratoScale = src.vibratoScale;
    result.driftScale = src.driftScale;
    result.transitionSmoothness = src.transitionSmoothness;
    result.attackHoldSec = src.attackHoldSec;
    result.releaseHoldSec = src.releaseHoldSec;
    result.gainDb = src.gainDb;
    result.formantShiftCents = src.formantShiftCents;
    result.formantFollow = src.formantFollow;
    result.consonantGainDb = src.consonantGainDb;
    result.breathGainDb = src.breathGainDb;
    result.sibilanceGainDb = src.sibilanceGainDb;
    return result;
}

float pitchDeviationCents(const Note& note, SnapMode mode, int root, ScaleType scale) noexcept
{
    const float midi = note.targetMidi();
    const float snapped = snapMidi(midi, mode == SnapMode::Off ? SnapMode::Semitone : mode, root, scale);
    return (midi - snapped) * 100.0f;
}

std::vector<std::pair<NoteId, NoteEdit>> computeGuidePitchEdits(const std::vector<Note>& notes,
                                                                const std::vector<NoteId>& ids,
                                                                const std::vector<Note>& guideNotes,
                                                                const GuideFollowParams& params,
                                                                int* matchedOut)
{
    std::vector<std::pair<NoteId, NoteEdit>> edits;
    const float strength = std::clamp(params.strength, 0.0f, 1.0f);
    int matched = 0;

    for (const auto& n : notes)
    {
        if (! selected(ids, n.id))
            continue;

        // ガイドの時間軸での、このノートの区間
        const double start = n.outStartSec() + params.offsetSec;
        const double end = n.outEndSec() + params.offsetSec;
        const double duration = end - start;

        const Note* best = nullptr;
        double bestOverlap = 0.0;

        for (const auto& g : guideNotes)
        {
            const double overlap = std::min(end, g.endSec) - std::max(start, g.startSec);

            if (overlap > bestOverlap)
            {
                bestOverlap = overlap;
                best = &g;
            }
        }

        if (best == nullptr || ! (duration > 0.0) || bestOverlap < duration * params.minOverlap)
            continue;

        // 目標の音高（MIDI ノート番号、小数）
        float target = best->detectedMidi;

        if (params.mode == GuideFollowParams::Mode::KeepInterval)
            target += std::round(n.detectedMidi - best->detectedMidi);

        NoteEdit edit = n.edit;
        edit.pitchOffsetCents = (target - n.detectedMidi) * 100.0f * strength;
        edits.emplace_back(n.id, edit);
        ++matched;
    }

    if (matchedOut != nullptr)
        *matchedOut = matched;

    return edits;
}
} // namespace koenote::model
