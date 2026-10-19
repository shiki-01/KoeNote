# 設計メモ（フェーズ2 時点）

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

## 8. フェーズ2 の追加

### 8.1 編集モデル v2

- 保存形式をバージョン 2 へ。ノートの編集項目に、ゲイン、フォルマントシフト、フォルマントのピッチ追従、子音のゲイン、ブレスのゲインを追加。
  モデルには、MIDI の目標ノートと割り当て（Undo の対象）、設定（レンダリング方式、スナップ、キー、グリッド、手動 BPM。Undo の対象外）も保存する。
- **v1（フェーズ1 の保存データ）は、読み込み時に v2 へ移行する**（追加項目は既定値）。手で組み立てた v1 のバイト列でテストしている。
  範囲外の設定値は既定へ戻す。ARA のアーカイブ形式（`kArchiveFormatVersion`）は 1 のまま（中身のモデルが自身のバージョンを持つため）。
- `applyEdits` は複数ノートの編集を 1 回の Undo にまとめ、タイミングの重なりは「長さを保ったまま後ろへ押し出す」。

### 8.2 レンダリングの追加（F-09・F-10）

- **フォルマント**: grain を時間軸方向に `formantScale` 倍して読み、窓の長さを 1/formantScale 倍にする。窓は出力の周期以上にそろえる
  （そうしないと窓が途切れて脈動する）。±600 セントに制限している（TD-PSOLA で、それ以上は基本周波数の聞こえ方が崩れる）。
  「ピッチ追従」は、フォルマントのシフトにピッチ補正量の割合を加える。
- **ゲイン**: `GainEnvelope` が、ノート・子音（ノート直前の最大 80 ms）・ブレス（ノート直後から次の子音の手前）のゲインを、
  出力時間軸の曲線（2 ms 刻み、約 10 ms で平滑化）にする。grain ごとに掛けるため、無編集の区間は元音声と一致する。

### 8.3 MIDI（F-11・F-12）

- 抽出: `notesToMidi`（音高は四捨五入、ベロシティは音量から 20〜127）、`pitchBendCurve`（ノート中心からの偏差）。
  SMF は `midi::writeSmf/readSmf`（format 0 の書き出し、format 0/1/2 の読み込み。テンポ変化、pitch bend、running status、壊れたデータへの耐性）。
  DAW へは、Editor のドラッグ元から外部ドラッグ&ドロップ（JUCE の `performExternalDragDropOfFiles`）。確実な代替として「保存…」を用意している。
  テンポマップがあれば、クリップの位置でのテンポとその後の変化を SMF に書き込む（DAW のグリッドに揃えるため）。
- 適用: `importMidi` が、解析ノートと MIDI ノートを DP（Needleman-Wunsch 型。開始時刻・音高の許容つき）で対応付ける。先頭を合わせるオフセットは自動計算。
  `computeTargetEdits` が、強度（ピッチ・タイミング別）に応じて編集を作る。手動の割り当ては `assignTarget`。

### 8.4 テンポ・グリッド・一括補正・キー（F-14・F-15・F-16）

- `TempoMap`（秒 ↔ 四分音符の区分線形）。ARA では、リージョンの MusicalContext から `kARAContentTypeTempoEntries` を読む（1 秒ごとにキャッシュ）。
  ホストが提供しなければ、手動 BPM の一定テンポ。UI のグリッド線（拍・小節）と、クオンタイズが同じ表を使う。
- `computeBatchEdits`: ピッチは検出値からスナップした音へ strength の割合だけ寄せる（スナップ: なし／半音／スケール）、ビブラートの保持、
  タイミングはグリッドへ寄せる（長さを保つ／終了位置も寄せる）。
- キー検出は Krumhansl-Schmuckler 法（ノートの長さで重み付け）。結果はスナップのスケールに設定する。

### 8.5 整列（F-13）

`alignAudio`: MFCC（13 次）+ 対数エネルギー、帯付き DTW（Sakoe-Chiba、半径 6 秒）。ガイドは、ドキュメント内の別の AudioSource（読み込み済みのもの）、
または音声ファイル。結果の対応表で各ノートの区間をガイドの時間へ移し、`applyEdits` で 1 回の Undo にまとめる。

### 8.6 A/B 比較（F-18）

再生レンダラーは、AudioModification の時間の範囲 [start, end] を、ブロックごとに 3 区間に分けて、範囲内を元の音声、範囲外を編集後の音声にする
（範囲指定なしなら全体がバイパス）。切り替えは atomic の状態を読むだけで、ロックはしない。

### 8.7 ニューラル加工（F-17）

- `neural/`: 対数メル（Slaney、反射パディング、PC-NSF-HiFiGAN の設定）、ONNX Runtime の動的ロード（C API ヘッダのみ `third_party/onnxruntime` に同梱。
  `onnxruntime.dll` と重みは利用者が配置）、NSF-HiFiGAN 系のボコーダー（チャンク + context で処理）。
- 合成の手順: 元音声（モノラル、44.1 kHz へ変換）→ メル → 出力時間軸へ時間伸縮（TimeMap）+ メル軸のずらし（フォルマント）+ f0（編集後）→ ボコーダー → 元のレートへ → ゲイン曲線。
- **再生の切り替え**: 編集のたびにまず TD-PSOLA（軽量）を公開し、編集が 0.5 秒止まったらニューラルを始める。ニューラルの結果は、最新の編集（revision）に
  対するものだけを再生に使い、古ければ TD-PSOLA の結果で補う。実行中の合成は、新しい要求が来ると取り消される。
- セットアップと制約は [neural-setup.md](neural-setup.md)。

### 8.8 RenderEngine

解析・整列・レンダリング・ニューラルの worker thread と、結果の公開（ProcessingGate）を `RenderEngine` に分離した（ARA 非依存。`ISourceAudio` 越しに元音声を得る）。
`KoeNoteAudioModification` は、モデル、ホストへの通知、テンポ・リージョンの情報などの ARA 固有の処理を担当する。
`tests/engine` の結合テスト（JUCE コンソールアプリ）が、スタブの音声でこの経路を検証する。

### 8.9 精度（解析層の改善）

合成歌声での評価（`tests/test_accuracy.cpp`）をもとに、次を変更した。
- YIN の窓長を 3/fMin から 1.5/fMin へ（ポルタメントの立ち上がりで無声になる問題の解消。解析時間も約半分に）。有声判定のしきい値は 0.35。
- ノート分割: 同じ音高の連続するノートを音量の谷で分ける。音高変化の境界は、弱い平滑化の曲線が元の音高から離れ始めた位置まで戻す。
- 結果（合成歌声）: RPA 98.3%、オクターブ誤り 0%、COnP F 値 0.99（低い声 D2〜G3 でも RPA 95.5%、F 値 0.91）。解析速度は 3 分の音声で約 0.8 秒。

## 9. 既知の制約（フェーズ2 時点）

- リージョンのタイムストレッチ（ホスト側）は未対応。ノートの時間移動は、ホストが定めるリージョンの範囲内で聞こえる。
- サンプルレートが異なる素材は線形補間で再生（簡易）。
- フォルマントのシフトは ±600 セント（TD-PSOLA の限界）。ニューラル加工ではメル軸のずらしで ±1 オクターブまで。
- ニューラル加工はモノラル出力で、処理は CPU。書き出し前に、ステータスが「反映済み」になるのを待つ必要がある（待たないと TD-PSOLA の結果になる）。
- 編集のたびに全体をレンダリングし直す（合成した 3 分のモノラル音声で TD-PSOLA 約 0.15 秒、ニューラル約 30 秒の見込み）。
  元音声・解析用のモノラル・レンダリング結果を保持するため、10 分のステレオ（48 kHz）で約 600 MB（ニューラルを使うとさらに増える）。
- 整列の帯の半径（6 秒）を超えるずれは正しく整列できない。定常音の内部では対応位置が一意に決まらない（ノートの開始位置は 40 ms 以内）。
- MIDI のドラッグ&ドロップは、ホストが外部ファイルのドロップを受け付けるかに依存する（代替: 「保存…」）。
- 実音声（本人の歌声・ボカロ）でのピッチ推定・ノート分割の精度は未評価（`koenote_analyze` と `tools/eval` で評価できる。評価データが必要）。
- Cakewalk Sonar での確認は、フェーズ0・1 の項目のみ実施済み（フェーズ2 の項目は未確認）。
