#include "model/PitchDrawing.h"

#include <algorithm>
#include <cmath>

namespace koenote::model
{
namespace
{
bool validMidi(float v) noexcept { return std::isfinite(v) && v > 0.0f && v < 140.0f; }
} // namespace

void PitchDrawing::draw(std::uint32_t firstFrame, const std::vector<float>& values)
{
    if (values.empty() || ! std::all_of(values.begin(), values.end(), validMidi))
        return;

    const auto endFrame = firstFrame + static_cast<std::uint32_t>(values.size());

    // 重なる・隣接する区間を集めて、1 つの区間にまとめる
    std::uint32_t mergedStart = firstFrame;
    std::uint32_t mergedEnd = endFrame;

    for (const auto& s : list)
    {
        if (s.endFrame() >= firstFrame && s.startFrame <= endFrame)
        {
            mergedStart = std::min(mergedStart, s.startFrame);
            mergedEnd = std::max(mergedEnd, s.endFrame());
        }
    }

    Segment merged;
    merged.startFrame = mergedStart;
    merged.midi.assign(mergedEnd - mergedStart, 0.0f);

    std::vector<Segment> rest;
    rest.reserve(list.size() + 1);

    for (auto& s : list)
    {
        if (s.endFrame() >= firstFrame && s.startFrame <= endFrame)
            std::copy(s.midi.begin(), s.midi.end(), merged.midi.begin() + (s.startFrame - mergedStart));
        else
            rest.push_back(std::move(s));
    }

    std::copy(values.begin(), values.end(), merged.midi.begin() + (firstFrame - mergedStart));
    rest.push_back(std::move(merged));

    std::sort(rest.begin(), rest.end(), [] (const Segment& a, const Segment& b) { return a.startFrame < b.startFrame; });
    list = std::move(rest);
}

void PitchDrawing::erase(std::uint32_t firstFrame, std::uint32_t endFrame)
{
    if (endFrame <= firstFrame)
        return;

    std::vector<Segment> result;
    result.reserve(list.size() + 1);

    for (auto& s : list)
    {
        if (s.endFrame() <= firstFrame || s.startFrame >= endFrame)
        {
            result.push_back(std::move(s));
            continue;
        }

        // 消す範囲の前と後ろを残す
        if (s.startFrame < firstFrame)
        {
            Segment head;
            head.startFrame = s.startFrame;
            head.midi.assign(s.midi.begin(), s.midi.begin() + (firstFrame - s.startFrame));
            result.push_back(std::move(head));
        }

        if (s.endFrame() > endFrame)
        {
            Segment tail;
            tail.startFrame = endFrame;
            tail.midi.assign(s.midi.begin() + (endFrame - s.startFrame), s.midi.end());
            result.push_back(std::move(tail));
        }
    }

    list = std::move(result);
}

bool PitchDrawing::midiAt(std::size_t frame, float& midi) const noexcept
{
    // 区間は昇順。frame を含みうる最後の区間を二分探索する
    auto it = std::upper_bound(list.begin(), list.end(), frame, [] (std::size_t f, const Segment& s) { return f < s.startFrame; });

    if (it == list.begin())
        return false;

    --it;

    if (frame >= it->endFrame())
        return false;

    midi = it->midi[frame - it->startFrame];
    return true;
}

std::size_t PitchDrawing::totalFrames() const noexcept
{
    std::size_t total = 0;

    for (const auto& s : list)
        total += s.midi.size();

    return total;
}

bool PitchDrawing::fromSegments(std::vector<Segment> segments, PitchDrawing& out)
{
    for (std::size_t i = 0; i < segments.size(); ++i)
    {
        const auto& s = segments[i];

        if (s.midi.empty() || ! std::all_of(s.midi.begin(), s.midi.end(), validMidi))
            return false;

        // 昇順で、重ならず、隣接もしない
        if (i > 0 && s.startFrame <= segments[i - 1].endFrame())
            return false;

        // endFrame の桁あふれ
        if (static_cast<std::uint64_t>(s.startFrame) + s.midi.size() > 0xFFFFFFFFull)
            return false;
    }

    out.list = std::move(segments);
    return true;
}

bool PitchDrawing::operator==(const PitchDrawing& other) const noexcept
{
    if (list.size() != other.list.size())
        return false;

    for (std::size_t i = 0; i < list.size(); ++i)
        if (list[i].startFrame != other.list[i].startFrame || list[i].midi != other.list[i].midi)
            return false;

    return true;
}
} // namespace koenote::model
