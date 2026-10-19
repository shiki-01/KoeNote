#pragma once

#include "model/Note.h"

#include <vector>

namespace koenote::render
{
// 出力時間（編集後の時間軸）から元音声の時間（秒）への区分線形な対応表。
// ノートのタイミング編集（F-06）をレンダリングに伝えるために使う。
class TimeMap
{
public:
    struct Anchor
    {
        double outSec = 0.0;
        double srcSec = 0.0;
    };

    // anchors は outSec・srcSec ともに単調非減少であること（違反する場合は直前の値に丸める）。
    // 空の場合は恒等写像。最初のアンカーより前、最後のアンカーより後は傾き 1（平行移動）で外挿する。
    explicit TimeMap(std::vector<Anchor> anchorsIn = {});

    // ノート列から対応表を作る。ノートの前後や間は次の規則で補う。
    //  - 最初のノートより前、最後のノートより後: 平行移動
    //  - ノート間（子音・ブレス・無音）: 線形に伸縮する。ただし直後のノートの手前 consonantSec までは
    //    ノートに付随させて伸縮させない（子音部の保護）。
    static TimeMap fromNotes(const std::vector<model::Note>& notes, double consonantSec = 0.06);

    double toSource(double outSec) const noexcept;
    // outSec 付近での d(src)/d(out)。傾き 0 は「元音声を使わない区間」を表す。
    double slopeAt(double outSec) const noexcept;
    // 元音声の時刻に対応する出力時刻（逆写像。傾き 0 の区間では最初の出力時刻）
    double toOutput(double srcSec) const noexcept;

    bool isIdentity() const noexcept { return anchors.empty(); }
    const std::vector<Anchor>& getAnchors() const noexcept { return anchors; }

private:
    std::vector<Anchor> anchors;
};
} // namespace koenote::render
