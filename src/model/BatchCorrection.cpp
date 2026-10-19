#include "model/BatchCorrection.h"

#include <algorithm>
#include <cmath>

namespace koenote::model
{
std::vector<std::pair<NoteId, NoteEdit>> computeBatchEdits(const std::vector<Note>& notes,
                                                           const std::vector<NoteId>& ids,
                                                           const BatchParams& params)
{
    std::vector<std::pair<NoteId, NoteEdit>> edits;
    const float pitchStrength = std::clamp(params.pitchStrength, 0.0f, 1.0f);
    const double timingStrength = std::clamp(static_cast<double>(params.timingStrength), 0.0, 1.0);
    const bool quantize = timingStrength > 0.0 && params.tempo.valid() && params.gridQuarters > 0.0;

    for (const auto& note : notes)
    {
        if (! ids.empty() && std::find(ids.begin(), ids.end(), note.id) == ids.end())
            continue;

        NoteEdit edit = note.edit;

        // ピッチ: 検出したピッチ中心を、スナップした音へ strength の割合だけ近づける
        const float snapped = snapMidi(note.detectedMidi, params.snapMode, params.snapRoot, params.snapScale);
        edit.pitchOffsetCents = (snapped - note.detectedMidi) * 100.0f * pitchStrength;
        edit.centerCorrection = pitchStrength;
        edit.vibratoScale = std::clamp(params.vibratoKeep, 0.0f, 2.0f);

        // タイミング: 開始位置（と、必要なら終了位置）をグリッドへ近づける
        if (quantize)
        {
            const double start = note.outStartSec();
            const double end = note.outEndSec();
            const double newStart = start + (snapToGrid(start, params.tempo, params.offsetSec, params.gridQuarters) - start) * timingStrength;
            double newEnd = end;

            if (params.keepDuration)
                newEnd = newStart + (end - start);
            else
                newEnd = end + (snapToGrid(end, params.tempo, params.offsetSec, params.gridQuarters) - end) * timingStrength;

            newEnd = std::max(newEnd, newStart + 0.03);

            if (std::abs(newStart - start) > 1.0e-9 || std::abs(newEnd - end) > 1.0e-9)
            {
                edit.targetStartSec = std::max(0.0, newStart);
                edit.targetEndSec = std::max(0.0, newStart) + (newEnd - newStart);
            }
        }

        edits.emplace_back(note.id, edit);
    }

    return edits;
}
} // namespace koenote::model
