#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace koenote::model
{
// 手描きしたピッチ曲線（F-22）。PitchTrack のフレーム（元音声の時間軸）ごとの目標の音高（MIDI ノート番号、小数）を、
// 描いた区間だけ保持する。描いた区間では、ノートの編集から求めたピッチより優先される（無声のフレームは変えられない）。
// 区間は startFrame の昇順で、重ならず、隣接もしない（隣接する区間は 1 つにまとめる）。
class PitchDrawing
{
public:
    struct Segment
    {
        std::uint32_t startFrame = 0;
        std::vector<float> midi;

        std::uint32_t endFrame() const noexcept { return startFrame + static_cast<std::uint32_t>(midi.size()); } // 含まない
    };

    bool empty() const noexcept { return list.empty(); }
    const std::vector<Segment>& segments() const noexcept { return list; }

    // firstFrame から values を描く（既存の値は上書きする）。値が有限でない、または空なら何もしない。
    void draw(std::uint32_t firstFrame, const std::vector<float>& values);
    // [firstFrame, endFrame) の描いた値を消す
    void erase(std::uint32_t firstFrame, std::uint32_t endFrame);
    void clear() noexcept { list.clear(); }

    // frame の描いた音高。描いていなければ false
    bool midiAt(std::size_t frame, float& midi) const noexcept;

    // 全区間のフレーム数の合計
    std::size_t totalFrames() const noexcept;

    // 区間を検査して作る（不正なら false。保存データの読み込み用）
    static bool fromSegments(std::vector<Segment> segments, PitchDrawing& out);

    bool operator==(const PitchDrawing& other) const noexcept;

private:
    std::vector<Segment> list;
};
} // namespace koenote::model
