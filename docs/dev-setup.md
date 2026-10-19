# 開発環境と動作確認の手順

## 1. 必要なもの

- Windows 10/11（x64）
- Visual Studio 2022（C++ によるデスクトップ開発、または Build Tools 2022）
- CMake 3.22 以降
- Git
- 動作確認用: Cakewalk Sonar（無料版）。第二の検証環境として REAPER

## 2. ビルド

```bash
git clone --recursive <このリポジトリの URL>
# 既に clone 済みの場合
git submodule update --init --recursive

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

- 初回の configure は JUCE の `juceaide` をビルドするため数分かかります。
- submodule は JUCE 8.0.15、ARA SDK releases/2.3.0（ARA SDK 自体の submodule を含む）、Catch2 v3.16.1 に固定しています。
- プラグインだけ、またはテストだけをビルドする場合は `-DKOENOTE_BUILD_TESTS=OFF` / `-DKOENOTE_BUILD_PLUGIN=OFF` を指定します。

### ビルド成果物

| 成果物 | パス |
|---|---|
| VST3 | `build/src/plugin/KoeNote_artefacts/Release/VST3/KoeNote.vst3` |
| テスト | `build/tests/Release/koenote_tests.exe` |

## 3. VST3 の配置

次のいずれかの方法で、DAW がスキャンするフォルダに `KoeNote.vst3` を置きます。

- 手動コピー: `KoeNote.vst3`（フォルダ形式のバンドル）を `C:\Program Files\Common Files\VST3\` へコピーします（管理者権限が必要）。
- 自動コピー: `-DKOENOTE_COPY_PLUGIN=ON` を付けて configure し直すと、ビルド後に JUCE が VST3 フォルダへコピーします。
  コピー先に書き込めない場合はビルドが失敗するため、その場合は手動コピーを使ってください。

DAW が起動中は `.vst3` がロックされ、コピーに失敗することがあります。ビルド前に DAW を終了してください。

## 4. Cakewalk Sonar での確認手順

### 4.1 プラグインのスキャン

1. Sonar を起動し、Preferences（環境設定）→ VST Settings（プラグイン設定）を開きます。
2. Scan Paths に `C:\Program Files\Common Files\VST3` が含まれていることを確認します。
3. 「Scan」を実行します。再スキャンで KoeNote が表示されない場合は、Options で「Re-scan Failed Plug-ins」を実行し、
   Sonar のスキャンログ（`Plugin Manager` 内の失敗リスト）を確認してください。

### 4.2 ARA プラグインとしてクリップに割り当てる

1. オーディオトラックにボーカルのクリップを配置します（モノラル・ステレオのどちらでも可。単旋律のボーカルを推奨）。
   プロジェクトと素材のサンプルレートが異なる場合は線形補間で再生しますが、品質は簡易的です（同じサンプルレートを推奨）。
2. 対象のクリップを選択し、右クリックまたはクリップのメニューから ARA 関連のコマンド（例: 「Audio Random Access」→「KoeNote」）を選びます。
   Sonar のバージョンによってメニュー名が異なる場合があります。ARA 対応プラグインはトラックの FX ビンではなく、
   クリップ（Region）に対して割り当てる点が通常のエフェクトと異なります。
3. 割り当てると KoeNote の Editor が開きます。ホストから音声を取得すると自動的に解析が始まり、
   下部のステータスに「解析中… n%」→「解析済み - N ノート」と表示されます（3 分のボーカルで数秒〜十数秒が目安）。
4. 解析後、ピアノロールにノートのブロックとピッチ曲線が表示されます。編集の操作は 4.3 を参照してください。
5. 再生して、編集内容が反映されることを確認します。編集直後は、レンダリングが終わるまで直前の音が再生され（ステータスに「レンダリング中…」）、
   完了すると切り替わります。

### 4.3 Editor の操作

| 操作 | 内容 |
|---|---|
| ノートを上下にドラッグ | ピッチ移動（スナップ: なし／半音／スケール。スケールはルート音とメジャー／マイナーを選択） |
| Alt を押しながらドラッグ | ノートを時間方向へ移動 |
| ノートの左右の端をドラッグ | ノートの長さ（開始・終了位置）を変更。タイムストレッチで反映（子音部は伸縮を抑えます） |
| ダブルクリック | そのノートの編集をリセット |
| 「分割」ツール（または S キー） | ノートをクリック（マウス位置）で分割 |
| 「結合」ボタン（または J キー） | 選択ノートを直後のノートと結合（複数選択時は選択中のノート同士を結合） |
| 「境界」ツール | 隣り合うノートの境界をドラッグして移動 |
| Ctrl+Z / Ctrl+Y（Ctrl+Shift+Z） | 元に戻す / やり直し |
| Delete | 選択ノートの編集をリセット |
| Ctrl+A / Shift・Ctrl+クリック / ドラッグ | 全選択 / 追加選択 / 範囲選択 |
| ホイール / Shift+ホイール / Ctrl+ホイール | 縦スクロール / 横スクロール / 横ズーム（Ctrl+Shift+ホイールで縦ズーム） |
| 右ボタン（中ボタン）ドラッグ | 表示のパン |
| 右側パネル「ノート」 | 選択ノートのピッチ中心の補正、ビブラート量、ピッチドリフト、遷移の滑らかさ、フォルマントシフト（±600 セント）、フォルマントのピッチ追従、ゲイン、子音のゲイン（ノートの直前）、ブレスのゲイン（ノートの直後） |
| 右側パネル「一括補正」 | ピッチ補正の強さ（半音／スケールへ寄せる）、ビブラートの保持、クオンタイズの強さ（グリッド: 4〜32 分音符、長さを保つ／終了位置も寄せる）。「選択ノートに適用」「全ノートに適用」。「キー・スケールを検出」でスナップのスケールを設定。ホストのテンポがない場合は BPM を入力 |
| 右側パネル「MIDI」 | 「ここからドラッグして DAW へ」で元音源のノートを SMF として渡す（ピッチベンド・編集後のノートも選択可）。「保存…」でファイルへ。「読み込み…」またはウィンドウへのドロップで MIDI を目標として取り込み、ピッチ・タイミングの適用強度を指定して「適用」。対応付けできないノートは「割り当て」ツールで手動で対応付け |
| 右側パネル「その他」 | A/B 比較（元の音声で聴く。選択ノートの範囲だけも可）、タイミング整列（ガイドの音声に DTW で合わせる。ドキュメント内の別のクリップ、またはファイルから。「開始位置のずれ」で、ガイドと頭がずれている場合の開始位置を 10 ms 刻みで指定でき、「ずれだけ適用」で整列せずにノート全体を動かせる）、ニューラル加工（[neural-setup.md](neural-setup.md)） |
| 定規・グリッド | ホストのテンポがあれば、小節・拍の定規とグリッド線だけを表示（4/4 として数える）。テンポがない場合のみ、秒の定規になる（「一括補正」で BPM を入力すると小節・拍になる） |
| ツールバーの「A/B」 | 元の音声と編集後の音声を即座に切り替えて聴き比べる |
| 「割り当て」ツール | ノートを選び、MIDI の目標（枠線）をクリックして対応付ける |
| 「再解析」 | 編集内容をすべて破棄し、音声を解析し直す |
| 「English」/「日本語」 | UI 言語の切り替え |

ホストによっては、プラグイン Editor 上のキー入力がホストへ渡ることがあります。その場合は、ツールバーのボタンを使ってください。

### 4.4 確認項目

**フェーズ0（ARA 接続・パススルー）**

- [ ] Sonar が KoeNote を VST3 としてスキャンできる
- [ ] クリップへ割り当てると Editor が開く（「ARA 非対応のホストです」と表示されない）
- [ ] 編集前は元の音声が欠けずに聞こえる（再生開始直後の数十ミリ秒が無音になる場合は、読み込み完了前の可能性があるため報告してください）
- [ ] クリップを移動・トリムしても再生が追従する
- [ ] プロジェクトを保存して開き直しても、クリップへの割り当てと編集内容が保持される
- [ ] ミックスダウン（書き出し）で、再生時と同じ音声が得られる

**フェーズ1（MVP）**

- [ ] 割り当て後に自動で解析が行われ、ノートが表示される
- [ ] ノートのピッチ移動が再生に反映される（フォルマントが保たれ、不自然な音色変化が小さい）
- [ ] ノートの長さ・位置の変更が再生に反映される
- [ ] 分割・結合・境界の移動ができる
- [ ] Undo / Redo が動作する
- [ ] 保存 → 開き直しで編集内容が復元される
- [ ] 1 曲のボーカルを補正してミックスダウンに反映できる（フェーズ1 の完了条件）
- [ ] 編集操作中に再生が途切れない（音切れ・ノイズ）

**フェーズ2（v1.0 の機能）**

- [ ] ノートのフォルマントシフト・ゲイン・子音/ブレスのゲインが再生に反映される
- [ ] 「一括補正」でピッチとタイミングを補正できる。グリッドが Sonar のテンポ・拍に合っている（テンポを変更したプロジェクトも確認）
- [ ] 「キー・スケールを検出」の結果が妥当で、スケールへのスナップが効く
- [ ] MIDI のドラッグ&ドロップで、Sonar の MIDI トラックへノートが取り込まれる（ドロップできない場合は「保存…」で書き出して取り込む）。ノートの位置が元のクリップと合っている
- [ ] MIDI ファイルを読み込み、ピッチ・タイミングが目標に合う。未対応のノートを「割り当て」ツールで対応付けられる
- [ ] ダブルのトラックをガイド（リードボーカル）に整列でき、タイミングが揃う
- [ ] A/B 比較（バイパス、選択範囲のみ）で音が切り替わり、ノイズが出ない
- [ ] （ニューラルを配置した場合）ニューラル加工が反映され、書き出しにも反映される。ステータスが「反映済み」になるまで待ってから書き出す
- [ ] 編集・保存・開き直し・書き出しの一連が、フェーズ1 のプロジェクト（保存済みの編集）でも問題なく動く（v1 の保存データは自動で移行される）

不具合があれば、Sonar のバージョン、素材のサンプルレート・チャンネル数、再現手順を記録してください。

## 5. pluginval による検査

CI と同じ条件でローカル検査する場合（pluginval を別途ダウンロードしておく）:

```bash
pluginval --strictness-level 5 --validate build/src/plugin/KoeNote_artefacts/Release/VST3/KoeNote.vst3
```

## 6. ARA Test Host による検査（任意）

ARA SDK 付属の ARA Test Host で、DAW なしに ARA プロトコルの適合を検査できます（ARA の呼び出しスレッドや順序の違反を検出します）。

```bash
# 1. VST3 SDK を clone（ARA Examples のビルドに必要。リポジトリ外に置く）
git clone --depth 1 --recurse-submodules --shallow-submodules https://github.com/steinbergmedia/vst3sdk.git <作業ディレクトリ>/vst3sdk

# 2. ARA Examples を configure し、TestHost だけをビルド
cmake -S external/ARA_SDK/ARA_Examples -B <作業ディレクトリ>/build -G "Visual Studio 17 2022" -A x64 -DARA_VST3_SDK_DIR=<作業ディレクトリ>/vst3sdk
cmake --build <作業ディレクトリ>/build --config Release --target ARATestHost -- /p:BuildProjectReferences=false

# 3. KoeNote の実体（バンドル内のバイナリ）を指定して実行
<作業ディレクトリ>/build/bin/Release/ARATestHost.exe -vst3 "build/src/plugin/KoeNote_artefacts/Release/VST3/KoeNote.vst3/Contents/x86_64-win/KoeNote.vst3" -test PropertyUpdates ContentUpdates ContentReading ModificationCloning Archiving SplitArchives DragAndDrop PlaybackRendering EditorView
```

- 手順 2 の `/p:BuildProjectReferences=false` は、サンプルプラグイン（ARATestPlugIn）の VST3 フォルダへのコピー（管理者権限が必要）を避けるためです。Git Bash では `MSYS_NO_PATHCONV=1` を付けてください。
- `-vst3` にはバンドルのフォルダではなく、内部の `.vst3` バイナリを指定します。
- 検証時点（フェーズ1）では、上記 9 つのシナリオがすべて ARA の assert なしで完了しました。
- `-file` で WAV を指定すると、環境によってはホスト側の読み込みで停止することがあったため、`-file` を省略（ホストが生成する 440 Hz のパルス正弦波を使用）しています。

## 7. UI のプレビューと性能計測（ホスト不要）

```bash
# 合成した歌声を解析 → 編集 → 描画し、PNG を出力する
build/tools/ui_preview/koenote_ui_preview_artefacts/Release/koenote_ui_preview.exe build/preview
# 3 分の音声で解析・レンダリングの所要時間を計測する
build/tools/ui_preview/koenote_ui_preview_artefacts/Release/koenote_ui_preview.exe --perf
```

## 8. 精度評価（mir_eval）

要件定義書の精度目標（RPA 95% 以上、オクターブ誤り 1% 未満、COnP F 値 80% 以上）は、次の 2 段で評価します。

1. **合成した歌声（自動テスト）**: `tests/test_accuracy.cpp`。男声〜女声の合成歌声（ビブラート・ポルタメント・ノイズ付き）で、
   mir_eval と同じ定義の指標を測り、目標を満たすことをテストで固定している。
2. **本人の歌声・公開データセット（手動）**: `koenote_analyze` で CSV を作り、`tools/eval/evaluate.py`（mir_eval）で測る。

```bash
# 音声 → ピッチ曲線とノートの CSV
build/tools/analyze/koenote_analyze_artefacts/Release/koenote_analyze.exe analyze vocal.wav out/
# 録音時の MIDI（歌った旋律）→ 正解ノートの CSV
build/tools/analyze/koenote_analyze_artefacts/Release/koenote_analyze.exe midi vocal_ref.mid out/vocal_ref_notes.csv
# mir_eval で評価（Python 3.10 以降、pip install -r tools/eval/requirements.txt）
python tools/eval/evaluate.py --ref-notes out/vocal_ref_notes.csv --est-notes out/vocal_notes.csv
```

正解のピッチ曲線がある場合は `--ref-pitch` / `--est-pitch` も使えます。詳細は `tools/eval/README.md` を参照してください。

## 9. ライセンス上の注意

- JUCE は AGPLv3 を選択しています。ソース公開が前提のため、ビルドした VST3 を配布する場合は対応するソースも提供してください。
- 学習済みモデルの重みはリポジトリに含めません（`.gitignore` で除外）。
