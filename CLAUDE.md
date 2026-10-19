# KoeNote

Melodyne 型のノート単位ボーカル編集プラグイン。ARA2 対応 VST3 として実装する。
要件の正は [docs/requirements.md](docs/requirements.md)。判断はこれに従い、要件と矛盾する指示やライブラリの制約に気づいた場合は実装せずに報告する。

## 現状

- フェーズ0（ARA パススルー）とフェーズ1（MVP: F-01〜F-08）の実装が完了。Cakewalk Sonar での実機確認は未実施（ユーザーが行う。手順は [docs/dev-setup.md](docs/dev-setup.md)）。
- 検証済み: Catch2 単体テスト、pluginval（strictness 5）、ARA SDK の ARA Test Host（9 シナリオ）。
- 設計は [docs/design.md](docs/design.md)（スレッド構成、データの流れ、各層の方式、既知の制約）。
- 要件定義書との差分: F0 推定は FCPE ではなく YIN ベースの pYIN 風（モデル不要）。FCPE（ONNX）はフェーズ2 で決める。
- 次フェーズ（フェーズ2: F-09〜F-18）で決める事項は、フェーズ1 完了時の報告と docs/design.md の「既知の制約」を参照。

## 技術スタック

- C++20、CMake、MSVC（Visual Studio 2022 / Build Tools）。対象は Windows x64 のみ。
- JUCE **8.0.15**（git submodule、`external/JUCE`。main ブランチは JUCE 9 のため、必ずタグで固定する）。ライセンスは AGPLv3 を選択。
- ARA SDK releases/2.3.0（submodule、`external/ARA_SDK`。ARA SDK 自体の submodule も取得）。`juce_set_ara_sdk_path()` と `IS_ARA_EFFECT TRUE` で ARA 対応。
- Catch2 v3.16.1（submodule）。精度評価は Python + mir_eval（`tools/eval`）。
- CI: GitHub Actions（windows-latest）でビルド、ctest、pluginval（strictness 5）。
- 基準 DAW: Cakewalk Sonar（無料版）。第二検証環境は REAPER。

## ディレクトリ構成

| パス | 内容 |
|---|---|
| `src/model/` | 編集モデル（EditModel: ノート・Undo/Redo・バージョン付きシリアライズ）。JUCE 非依存 |
| `src/analysis/` | 解析層（IPitchAnalyzer / INoteSegmenter、YinPitchAnalyzer、RuleBasedNoteSegmenter）。JUCE 非依存 |
| `src/render/` | レンダリング層（TD-PSOLA、TimeMap、ピッチ曲線編集、ProcessingGate、パススルー）。JUCE 非依存 |
| `src/plugin/` | AudioProcessor、ARA の DocumentController / AudioSource / AudioModification / PlaybackRenderer |
| `src/ui/` | ピアノロール、パラメータパネル、Editor、日英の文字列。`EditTarget` 越しにモデルへ触れる |
| `tests/` | Catch2 単体テスト（`koenote_core` に対して実行） |
| `tools/eval/` | mir_eval による精度評価（Python） |
| `tools/ui_preview/` | ARA ホストなしで UI を PNG 出力。`--perf` で解析・レンダリングの性能計測 |
| `docs/` | 要件定義書、設計メモ、開発・動作確認の手順 |

## 実装上の制約

- オーディオスレッドではメモリ確保、ロック、ファイル I/O、ログ出力を行わない（`ProcessingGate` の `tryRead()` と atomic のみ）。
- 解析・レンダリングは worker thread。モデルには触れず、値のコピーを使う。ARA の reader の生成・破棄は message thread、読み出しだけ別スレッド（ARA Test Host で検出された規則）。
- 編集データ（保存データ）にはバージョン番号を持たせ、旧バージョンのデータを新バージョンで読めるようにする（`kEditModelFormatVersion`。保存形式の固定テストあり）。
- 非破壊編集。元音声は変更せず、編集内容のみを ARA のアーカイブへ保存する。
- コードコメントは日本語。ライブラリ名・技術用語はカタカナにせず英字のまま書く。
- ライセンスは AGPLv3。`LICENSE`、`THIRD_PARTY_NOTICES.md` を維持する。依存ライブラリを追加したら notices も更新する。
- 学習済みモデルの重みはリポジトリに含めない（`.gitignore`）。非商用条件付きの重みは同梱しない。
- 新しいライブラリ・モデルは採用前にライセンスを確認する（GPL 系は AGPLv3 と整合する場合のみ）。
- `KOENOTE_WEBSITE`（ARA の informationURL。空は不可）は仮の値。公開時に実際の URL へ変更する。

## ビルド・テスト

```bash
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

- VST3: `build/src/plugin/KoeNote_artefacts/Release/VST3/KoeNote.vst3`。テスト: `build/tests/Release/koenote_tests.exe`。
- pluginval: `pluginval --strictness-level 5 --validate-in-process --validate <VST3>`。Editor の検査が 30 秒でタイムアウトしたことが一度あったが、再実行で成功した。
- ARA Test Host の手順は docs/dev-setup.md の 6 章、UI のプレビューは 7 章。
- Git Bash では `cmake --build ... -- /m` のようなスラッシュ引数がパス変換されるため、`--parallel` を使うか `MSYS_NO_PATHCONV=1` を付ける。
- テスト名が日本語のため、ctest は `catch_discover_tests` ではなく全体を 1 つの test として実行する。
- Bash ツールで長い heredoc や `node -e` を書くと構文エラー・停止になることがあった。ファイルの作成・編集は Write / Edit ツールを使う。

## 作業の進め方

- 段階ごとにコミットする。ビルド・テストが失敗したら原因を調べて修正してから次へ進む。
- DAW 実機確認はユーザーが行う。手順・確認項目は `docs/dev-setup.md`。

## 委任方針

委任には固定費（サブエージェント1回あたり数万トークンの起動コスト、外部モデルは往復10〜30秒）が
かかる。**大量に読む・大量に書く作業だけを委任し、小さな作業は自分で処理する。**
具体的な振り分けは `ext-delegate` スキルに従う。

- チェック実行: `check`（スクリプト。LLM を使わない）
- 大きなファイルの読み取り: `bulk-read`（外部許可時）→ 不可なら codebase-explorer / docs-reader
- 定型コードの大量出力: `code-write`（外部許可時）→ 不可なら test-scaffolder
- デバッグ・設計判断・安全性が重要なコード: 委任しない（bug-hunter / implementer も上位モデル）

外部モデルへの送信: 許可（`.claude/allow-external-llm` あり。gitignore 済み）。ただし `bulk-read` / `code-write` は
ローカルの OpenCode + Ollama を使う実装で、この環境には `opencode` と `ollama` が入っていないため現時点では使えない（Haiku サブエージェントを使う）。

利用可能なサブエージェント（`~/.claude/agents/`）:

| 名前 | 用途 |
|---|---|
| Explore | 読み取り専用の横断探索（Haiku 固定）。探索の広さを依頼に含める |
| codebase-explorer | 関数・型・定数等の所在をパスと行番号で返す読み取り専用探索 |
| docs-reader | docs/ の下読みと必要な節の要約。根拠を文書名＋節番号で返す |
| implementer | 方針確定後の実装（新規ファイル・機能追加・リファクタリング） |
| test-scaffolder | 既存テストの書き方に合わせたテスト雛形の作成。プロダクションコードは触らない |
| bug-hunter | エラー・テスト失敗の根本原因調査（修正はしない） |
