#pragma once

#include <cstddef>
#include <vector>

namespace koenote::model
{
// テンポ・グリッドの同期（F-14）。プロジェクトの時刻（秒）と四分音符単位の位置の対応表。
// ホスト（ARA の MusicalContext）のテンポ情報から作る。ホストが提供しない場合は、手動の BPM で一定テンポの表を作る。
class TempoMap
{
public:
    struct Entry
    {
        double sec = 0.0;     // プロジェクトの時刻（秒）
        double quarter = 0.0; // 四分音符単位の位置
    };

    TempoMap() = default;

    // entries は sec・quarter ともに増加する順。違反するものは捨てる。2 点未満なら無効。
    explicit TempoMap(std::vector<Entry> entries);

    // 一定テンポ（sec = 0 を quarter = 0 とする）
    static TempoMap constant(double bpm);

    bool valid() const noexcept { return table.size() >= 2; }

    // 区間の外は、端の区間のテンポで外挿する
    double secToQuarter(double sec) const noexcept;
    double quarterToSec(double quarter) const noexcept;
    double bpmAt(double sec) const noexcept;

    const std::vector<Entry>& entries() const noexcept { return table; }

    //==========================================================================
    // 拍子（ARA の kARAContentTypeBarSignatures と同じ規則）。各拍子は次の拍子の位置まで有効で、最初の拍子は
    // それより前にも適用される。最初の拍子の位置を小節 1 とする。拍子がなければ、quarter = 0 からの 4/4。
    struct BarSignature
    {
        double quarter = 0.0; // 拍子が始まる位置（四分音符単位）
        int numerator = 4;
        int denominator = 4;
    };

    // quarter が増加する順。不正なもの（分子・分母が 1 未満、分母が 2 の累乗でない、順序の違反）は捨てる。
    void setBarSignatures(std::vector<BarSignature> signatures);
    const std::vector<BarSignature>& barSignatures() const noexcept { return signatures; }

    struct MeterPosition
    {
        long long bar = 1; // 小節番号（1 始まり。最初の拍子より前は 0 以下）
        int beat = 1;      // 小節内の拍（1 始まり。拍は拍子の分母の音符）
        bool onBeat = false;
        bool onBar = false; // 小節の先頭
    };

    MeterPosition meterAt(double quarter) const noexcept;

    // その位置の拍子の、拍と小節の長さ（四分音符単位）
    double beatLengthAt(double quarter) const noexcept;
    double barLengthAt(double quarter) const noexcept;

    struct GridPosition
    {
        double quarter = 0.0;
        long long bar = 1;
        int beat = 1;
    };

    // [q0, q1] にある小節の先頭・拍の位置（最大 maxCount 個）
    std::vector<GridPosition> barsBetween(double q0, double q1, std::size_t maxCount = 4096) const;
    std::vector<GridPosition> beatsBetween(double q0, double q1, std::size_t maxCount = 4096) const;

private:
    struct Segment
    {
        double start = 0.0; // 四分音符単位
        double barLength = 4.0;
        double beatLength = 1.0;
        int beatsPerBar = 4;
        long long firstBar = 1; // start の小節番号
    };

    std::size_t segmentIndexAt(double quarter) const noexcept;
    std::vector<GridPosition> positionsBetween(double q0, double q1, std::size_t maxCount, bool barsOnly) const;
    void rebuildSegments();

    std::vector<Entry> table;
    std::vector<BarSignature> signatures;
    std::vector<Segment> segments { Segment {} }; // signatures から作る（空なら 4/4 の 1 区間）
};

// 時刻 sec を、四分音符 gridQuarters 刻みのグリッドへ丸める。
// offsetSec は「編集の時間軸（AudioModification の時間）→ プロジェクトの時刻」のずれ（リージョンの開始位置など）。
double snapToGrid(double sec, const TempoMap& tempo, double offsetSec, double gridQuarters) noexcept;
} // namespace koenote::model
