#pragma once

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

private:
    std::vector<Entry> table;
};

// 時刻 sec を、四分音符 gridQuarters 刻みのグリッドへ丸める。
// offsetSec は「編集の時間軸（AudioModification の時間）→ プロジェクトの時刻」のずれ（リージョンの開始位置など）。
double snapToGrid(double sec, const TempoMap& tempo, double offsetSec, double gridQuarters) noexcept;
} // namespace koenote::model
