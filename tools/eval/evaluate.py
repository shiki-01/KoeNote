#!/usr/bin/env python3
"""KoeNote の解析精度を mir_eval で評価する（要件定義書 4章・11章）。

入力は CSV。
  ピッチ曲線: 1 行 1 フレームで `time_sec,freq_hz`（無声は 0）
  ノート    : 1 行 1 ノートで `onset_sec,offset_sec,pitch_hz`

評価指標:
  - RPA（Raw Pitch Accuracy。正解から ±50 セント以内のフレームの割合）  目標 95% 以上
  - オクターブ誤り率（Raw Chroma Accuracy と RPA の差から算出）           目標 1% 未満
  - COnP F 値（ノートの開始時刻と音高が一致する割合）                    目標 80% 以上

使い方:
  python evaluate.py --ref-pitch ref.csv --est-pitch est.csv
  python evaluate.py --ref-notes ref_notes.csv --est-notes est_notes.csv
"""

from __future__ import annotations

import argparse
import json
import sys

import numpy as np

RPA_TARGET = 0.95
OCTAVE_ERROR_TARGET = 0.01
CONP_F_TARGET = 0.80


def load_csv(path: str, columns: int) -> np.ndarray:
    data = np.loadtxt(path, delimiter=",", ndmin=2, comments="#")
    if data.size == 0:
        return np.zeros((0, columns))
    if data.shape[1] != columns:
        raise ValueError(f"{path}: {columns} 列が必要です（実際は {data.shape[1]} 列）")
    return data


def evaluate_pitch(ref_path: str, est_path: str) -> dict:
    import mir_eval

    ref = load_csv(ref_path, 2)
    est = load_csv(est_path, 2)

    ref_time, ref_freq = ref[:, 0], ref[:, 1]
    est_time, est_freq = est[:, 0], est[:, 1]

    # 時間軸が異なる場合は、推定側を正解の時間軸へ補間する
    ref_voicing, ref_cent, est_voicing, est_cent = mir_eval.melody.to_cent_voicing(
        ref_time, ref_freq, est_time, est_freq
    )

    rpa = mir_eval.melody.raw_pitch_accuracy(ref_voicing, ref_cent, est_voicing, est_cent)
    rca = mir_eval.melody.raw_chroma_accuracy(ref_voicing, ref_cent, est_voicing, est_cent)
    voicing_recall = mir_eval.melody.voicing_recall(ref_voicing, est_voicing)

    # RCA は oct 違いを許容するため、RCA − RPA がオクターブ誤りの割合に相当する
    octave_error = max(0.0, float(rca) - float(rpa))

    return {
        "rpa": float(rpa),
        "rca": float(rca),
        "octave_error": octave_error,
        "voicing_recall": float(voicing_recall),
        "pass_rpa": bool(rpa >= RPA_TARGET),
        "pass_octave_error": bool(octave_error < OCTAVE_ERROR_TARGET),
    }


def merge_split_notes(notes: np.ndarray, max_gap: float) -> np.ndarray:
    """同じ音高（±10 セント）で隙間が max_gap 秒以下の連続したノートを 1 つにまとめる。

    歌声合成用の MIDI は子音と母音で別のノートに分けてあることがあり、そのままでは
    歌い手の 1 音が複数の正解ノートになるため。
    """
    if len(notes) == 0:
        return notes
    notes = notes[np.argsort(notes[:, 0])]
    merged = [notes[0].copy()]
    for note in notes[1:]:
        last = merged[-1]
        same_pitch = abs(1200.0 * np.log2(note[2] / last[2])) <= 10.0
        if same_pitch and note[0] - last[1] <= max_gap:
            last[1] = max(last[1], note[1])
        else:
            merged.append(note.copy())
    return np.array(merged)


def evaluate_notes(ref_path: str, est_path: str, merge_gap: float | None = None, ref_end: float | None = None) -> dict:
    import mir_eval

    ref = load_csv(ref_path, 3)
    est = load_csv(est_path, 3)

    # 音声が途中までしかない場合は、その範囲に始まる正解ノートだけを使う
    if ref_end is not None:
        ref = ref[ref[:, 0] < ref_end]
    ref_count = len(ref)
    if merge_gap is not None:
        ref = merge_split_notes(ref, merge_gap)

    ref_intervals, ref_pitches = ref[:, :2], ref[:, 2]
    est_intervals, est_pitches = est[:, :2], est[:, 2]

    # COnP: 開始時刻（±50 ms）と音高（±50 セント）が一致する割合。終了時刻は見ない。
    precision, recall, f_measure, _ = mir_eval.transcription.precision_recall_f1_overlap(
        ref_intervals,
        ref_pitches,
        est_intervals,
        est_pitches,
        onset_tolerance=0.05,
        pitch_tolerance=50.0,
        offset_ratio=None,
    )

    return {
        "ref_notes": int(ref_count),
        "ref_notes_used": int(len(ref)),
        "est_notes": int(len(est)),
        "conp_precision": float(precision),
        "conp_recall": float(recall),
        "conp_f": float(f_measure),
        "pass_conp_f": bool(f_measure >= CONP_F_TARGET),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ref-pitch")
    parser.add_argument("--est-pitch")
    parser.add_argument("--ref-notes")
    parser.add_argument("--est-notes")
    parser.add_argument("--merge-ref-notes", type=float, metavar="GAP_SEC",
                        help="同じ音高で隙間が GAP_SEC 秒以下の正解ノートをまとめる（子音・母音で分けた歌声合成用の MIDI 向け）")
    parser.add_argument("--ref-end", type=float, metavar="SEC", help="この時刻より前に始まる正解ノートだけを評価する（途中までの録音向け）")
    parser.add_argument("--fail-under-target", action="store_true", help="目標値を満たさない場合に終了コード 1 を返す（CI 用）")
    args = parser.parse_args()

    result: dict = {}

    if args.ref_pitch and args.est_pitch:
        result["pitch"] = evaluate_pitch(args.ref_pitch, args.est_pitch)
    if args.ref_notes and args.est_notes:
        result["notes"] = evaluate_notes(args.ref_notes, args.est_notes, args.merge_ref_notes, args.ref_end)

    if not result:
        parser.error("--ref-pitch/--est-pitch または --ref-notes/--est-notes を指定してください")

    print(json.dumps(result, indent=2, ensure_ascii=False))

    if args.fail_under_target:
        passed = all(v for section in result.values() for k, v in section.items() if k.startswith("pass_"))
        return 0 if passed else 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
