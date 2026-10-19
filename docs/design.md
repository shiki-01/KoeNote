# 設計メモ（フェーズ1 時点）

要件定義書（`requirements.md`）の 5章「アーキテクチャ」を、実装の観点で補足します。

## 1. 層とディレクトリ

| 層 | ディレクトリ | 依存 | 内容 |
|---|---|---|---|
| コア（JUCE 非依存） | `src/model`, `src/analysis`, `src/render` | C++20 標準のみ | 編集モデル、解析、レンダリング。`koenote_core`（静的ライブラリ）として単体テストする |
| プラグイン | `src/plugin` | JUCE（ARA） | AudioProcessor、ARA の DocumentController / AudioSource / AudioModification / PlaybackRenderer |
| UI | `src/ui` | JUCE（gui_basics） | ピアノロール、パラメータパネル。`EditTarget` 越しにモデルへ触れる（ARA 非依存） |
| 検証 | `tests`, `tools/eval`, `tools/ui_preview` | | Catch2、mir_eval、UI の PNG 出力 |

## 2. スレッド構成

| スレッド | 触れるもの | 禁止 |
|---|---|---|
| オーディオスレッド | `KoeNotePlaybackRenderer::processBlock`、レンダリング済みデータ／元音声の読み取り（atomic のポインタ） | メモリ確保、ロック、ファイル I/O、ログ出力 |
| message thread | `EditModel` の編集、UI、ARA モデルの変更、ホストへの通知、ARA の reader の生成・破棄 | |
| worker thread（AudioModification ごとに 1 本） | 解析（F0 推定・ノート分割）、レンダリング（TD-PSOLA）。モデルには触れず、値のコピーを使う | モデルの直接参照 |
| 読み込みスレッド（AudioSource ごと） | ホストの reader からの全サンプルの読み出し | |

### ProcessingGate

オーディオスレッドは mutex を使わず、`ProcessingGate`（atomic のカウンタ 2 つ）の `tryRead()` で入場する。
ARA モデルの編集中（`willBeginEditing` ～ `didEndEditing`）と、共有データ（元音声・レンダリング済み音声）の
差し替え中は writer が入場しており、オーディオスレッドは `tryRead()` に失敗して無音を返す。
writer は reader が退出するまで待つ（`yield` のスピン）。writer 同士は排他しない（互いに別のデータを書き換えるだけのため）。

### 結果の公開

worker thread が作った `RenderedClip`（編集済みの音声）は、`ProcessingGate` の write scope 内で
`std::atomic<const RenderedClip*>` を差し替えて公開する。旧データはゲートの外で解放する。
再レンダリング中は旧データが使われ続けるため、音切れは出ない（要件 4章「再生・レンダリング」）。
レンダリング要求はトークンで管理し、新しい要求が来ると実行中のレンダリングは中断され、古い結果は公開されない。

## 3. データの流れ

```
ホスト(ARA) ──サンプル──> KoeNoteAudioSource (読み込みスレッド)
                              │ 完了通知(message thread)
                              v
                    KoeNoteAudioModification ── 解析要求 ──> worker: YinPitchAnalyzer → RuleBasedNoteSegmenter
                              │ <──結果(message thread)──────────┘
                              v
                          EditModel ←── UI（ピアノロール、パネル）が編集
                              │ notifyEdited
                              v
                    worker: renderEditedAudio（TD-PSOLA）──> RenderedClip ──(gate)──> オーディオスレッド
                              │ 完了(message thread)
                              v
                    ホストへ notifyContentChanged(samples)
```

編集が 1 つもない場合は `renderEditedAudio` を呼ばず、元音声をそのまま再生する（ビット一致のパススルー）。

## 4. 解析層

- **F0 推定（`YinPitchAnalyzer`）**: YIN の差分関数・累積平均正規化に基づく pYIN 風の推定。約 16 kHz へ decimate し、
  各フレームの候補を Viterbi で平滑化して有声・無声を決める。hop 5 ms。要件定義書が主とする FCPE（ONNX）は、
  モデルの同梱条件の確認が必要なためフェーズ2 に回し、フェーズ1 は pYIN 風（モデル不要）で実装した。
- **ノート分割（`RuleBasedNoteSegmenter`）**: 有声区間を 100 ms の移動 median で平滑化し、半音の 0.7 を超える偏差が
  50 ms 続いたら新しいノートを開始する（ビブラートで分割されない）。60 ms 未満のノートは併合。
- どちらも `IPitchAnalyzer` / `INoteSegmenter` の実装で、将来 FCPE・GAME などへ差し替える。

## 5. 編集モデル層

- `EditModel` は 1 つの AudioModification に対応。ノート列と `PitchTrack`、Undo/Redo（スナップショット方式、最大 256 段）を持つ。
- ノートは「元音声上の区間（`startSec`/`endSec`）」と「編集後の区間（`edit.targetStartSec`/`targetEndSec`、出力時間軸）」を持ち、
  タイミング編集は後者だけを変える。不正な結果（重なり、最短 30 ms 未満、NaN）になる操作は拒否される。
- **保存形式**: `kEditModelFormatVersion`（現在 1）付きのバイナリ（little-endian）。新しいバージョンのデータは拒否し、
  旧バージョンは `deserialize` に読み込み処理を足して対応する（要件 4章「互換性」）。
  `tests/test_edit_model.cpp` のサイズ固定テストが、意図しない形式変更を検出する。
- ARA アーカイブ（`kArchiveFormatVersion`、ARA の archive ID は `...aradocumentarchive.1`）: AudioModification ごとに
  persistentID と `EditModel` のバイト列を書く。

## 6. レンダリング層（TD-PSOLA）

1. 元音声をモノラル化し、`PitchTrack` に沿ってピッチマークを置く（有声区間は波形のピークへ吸着、無声区間は 5 ms 間隔）。
2. `TimeMap`（出力時間 → 元音声の時間）をノートの編集から作る。ノート間（子音・ブレス・無音）は伸縮するが、
   直後のノートの手前 60 ms は伸縮させない（子音保護）。
3. `computePitchAdjustCents` が、ノートごとのピッチ移動・中心補正・ビブラート量・ドリフト量・遷移の滑らかさから
   各フレームのピッチ補正量（セント）を求める。
4. 出力のマークを `周期 / ピッチ比` ずつ進めながら、最寄りの元マークから Hann 窓付きで切り出して重ね合わせる。
   窓の重みの和が 1 になる構成のため、無編集の区間は元音声と一致する（テストで確認）。
- 全チャンネルに同じマークを使う。フォルマントは PSOLA の性質上おおむね保たれる（F-09 のフォルマント制御はフェーズ2）。
- 出力長は元の 2 倍 + 30 秒に制限する。

## 7. UI 層

`PianoRollView` は出力時間軸（編集後）で、ノートのブロック、編集後のピッチ曲線、元音声の波形（時間軸に合わせて変形）、
再生位置を描く。操作は `docs/dev-setup.md` の 4.3 を参照。ARA の選択（`ARAEditorView::Listener`）に追従して
編集対象の AudioModification を切り替える。ARA に依存する部分は `EditTarget`（抽象）で分離しており、
`tools/ui_preview` で ARA ホストなしに PNG を出力して見た目を確認できる。

## 8. 既知の制約（フェーズ1 時点）

- リージョンのタイムストレッチ（ホスト側）は未対応。ノートの時間移動は、ホストが定めるリージョンの範囲内で聞こえる。
- サンプルレートが異なる素材は線形補間で再生（簡易）。
- 編集のたびに全体をレンダリングし直す。合成した 3 分のモノラル音声で、解析 約 1.6 秒、レンダリング 約 0.14 秒（Release、`koenote_ui_preview --perf` で計測。実音声・ステレオでの計測は未実施）。
- 元音声・解析用のモノラル・レンダリング結果をメモリに保持するため、10 分のステレオ（48 kHz）で約 600 MB を使う。
- 実音声（本人の歌声・ボカロ）でのピッチ推定・ノート分割の精度は未評価（`tools/eval` の評価データが必要）。
- Cakewalk Sonar での動作は未確認（ARA Test Host による ARA プロトコルの検査のみ実施）。
