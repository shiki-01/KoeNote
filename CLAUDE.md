# KoeNote

Melodyne 型のノート単位ボーカル編集プラグイン。ARA2 対応 VST3 として実装する。
要件の正は [docs/requirements.md](docs/requirements.md)。判断はこれに従い、要件と矛盾する指示やライブラリの制約に気づいた場合は実装せずに報告する。

## 現状

- フェーズ0（ベース構築）着手前。ソースコード・CMake・CI はまだ存在しない。
- フェーズ0 の完了条件: Cakewalk Sonar 上で ARA によるクリップ音声の取得と再生差し替え（パススルー）ができること（要件 7章）。
- ピッチ解析・ピアノロール・加工処理はフェーズ0 の範囲外。interface と stub のみ置く。

## 技術スタック

- C++20、CMake、MSVC（Visual Studio 2022）。対象は Windows x64 のみ。
- JUCE 8（git submodule）。ライセンスは AGPLv3 を選択。
- ARA SDK（Celemony/ARA_SDK を git submodule。ARA SDK 自体の submodule も取得する）。
  - `juce_set_ara_sdk_path()` と `juce_add_plugin(... IS_ARA_EFFECT TRUE FORMATS VST3 ...)` で ARA 対応にする。
  - JUCE の ARAPluginDemo は参考にしてよいが、構成は本プロジェクト用に整理する。
- テスト: Catch2。精度評価: Python + mir_eval。
- CI: GitHub Actions（windows-latest）でビルド、pluginval（strictness level 5 以上）で VST3 を検査、Catch2 テストを実行。
- 基準 DAW: Cakewalk Sonar（無料版）。第二検証環境は REAPER。

## ディレクトリ構成（予定）

アーキテクチャは「解析 / 編集モデル / レンダリング / UI」の4層（要件 5章）。

| パス | 内容 |
|---|---|
| `src/plugin/` | AudioProcessor、Editor、ARA の DocumentController / PlaybackRenderer / EditorRenderer |
| `src/analysis/` | 解析層（F0・ノート推定。非同期実行前提の interface） |
| `src/model/` | 編集モデル層（ノート・編集パラメータ、Undo/Redo、シリアライズ） |
| `src/render/` | レンダリング層（フェーズ0 はパススルー） |
| `src/ui/` | UI 層（ピアノロール等。フェーズ0 は空の Editor） |
| `tests/` | Catch2 単体テスト |
| `tools/eval/` | mir_eval による精度評価スクリプト |
| `docs/` | 要件定義書、設計メモ、`dev-setup.md`（Sonar での確認手順） |

## 実装上の制約

- オーディオスレッドではメモリ確保、ロック、ファイル I/O、ログ出力を行わない。
- 解析・推論・描画はオーディオスレッドから分離し、別スレッドで非同期に行う前提で設計する。
- 編集データ（保存データ）にはバージョン番号を持たせ、旧バージョンのデータを新バージョンで読めるようにする。
- 非破壊編集。元音声は変更せず、編集内容のみを ARA の状態保存でプロジェクトに保持する。
- コードコメントは日本語。ライブラリ名・技術用語はカタカナにせず英字のまま書く。
- ライセンスは AGPLv3。`LICENSE` を置き、JUCE・ARA SDK・VST3 SDK の著作権表示を `THIRD_PARTY_NOTICES.md` にまとめる。
- 学習済みモデルの重みはリポジトリに含めない（`.gitignore` で除外）。非商用条件付きの重み（PC-NSF-HiFiGAN 等）は同梱しない。
- 新しいライブラリ・モデルは採用前にライセンスを確認する（GPL 系は AGPLv3 と整合する場合のみ）。

## ビルド・テスト

以下はフェーズ0 で整備する予定のコマンドであり、現時点では未作成（CMakeLists.txt 作成後に実際のものへ更新すること）。

```bash
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

- pluginval: `pluginval --strictness-level 5 --validate <VST3 のパス>`（CI と同条件）
- 精度評価: `tools/eval/` 配下（未作成）

## 作業の進め方

- 着手前にファイル構成と方針を短く提示し、確認を待つ。
- 段階ごと（最小ビルド → ARA パススルー → テスト・CI）にコミットする。
- ビルド・テストが失敗したら原因を調べて修正してから次へ進む。
- DAW 実機確認はユーザーが行う。手順は `docs/dev-setup.md` に書く。

## 委任方針

委任には固定費（サブエージェント1回あたり数万トークンの起動コスト、外部モデルは往復10〜30秒）が
かかる。**大量に読む・大量に書く作業だけを委任し、小さな作業は自分で処理する。**
具体的な振り分けは `ext-delegate` スキルに従う。

- チェック実行: `check`（スクリプト。LLM を使わない）
- 大きなファイルの読み取り: `bulk-read`（外部許可時）→ 不可なら codebase-explorer / docs-reader
- 定型コードの大量出力: `code-write`（外部許可時）→ 不可なら test-scaffolder
- デバッグ・設計判断・安全性が重要なコード: 委任しない（bug-hunter / implementer も上位モデル）

外部モデルへの送信: 許可（`.claude/allow-external-llm` あり。gitignore 済み）

利用可能なサブエージェント（`~/.claude/agents/`）:

| 名前 | 用途 |
|---|---|
| Explore | 読み取り専用の横断探索（Haiku 固定）。探索の広さを依頼に含める |
| codebase-explorer | 関数・型・定数等の所在をパスと行番号で返す読み取り専用探索 |
| docs-reader | docs/ の下読みと必要な節の要約。根拠を文書名＋節番号で返す |
| implementer | 方針確定後の実装（新規ファイル・機能追加・リファクタリング） |
| test-scaffolder | 既存テストの書き方に合わせたテスト雛形の作成。プロダクションコードは触らない |
| bug-hunter | エラー・テスト失敗の根本原因調査（修正はしない） |
