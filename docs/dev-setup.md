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
   プロジェクトと素材のサンプルレートが異なる場合は再生時に変換します（同じサンプルレートを推奨）。
2. 対象のクリップを選択し、右クリックまたはクリップのメニューから ARA 関連のコマンド（例: 「Audio Random Access」→「KoeNote」）を選びます。
   Sonar のバージョンによってメニュー名が異なる場合があります。ARA 対応プラグインはトラックの FX ビンではなく、
   クリップ（Region）に対して割り当てる点が通常のエフェクトと異なります。
3. 割り当てると KoeNote の Editor が開きます。ホストから音声を取得すると自動的に解析が始まり、
   下部のステータスに「解析中… n%」→「解析済み - N ノート」と表示されます（3 分のボーカルで数秒〜十数秒が目安）。
4. 解析後、ピアノロールにノートのブロックとピッチ曲線が表示されます。編集の操作は 4.3 を参照してください。
5. 再生して、編集内容が反映されることを確認します。編集直後は、レンダリングが終わるまで直前の音が再生され（ステータスに「レンダリング中…」）、
   完了すると切り替わります。

### 4.3 Editor の操作

操作の一覧は [user-guide.md](user-guide.md) の 4章（利用者向けの説明書）にまとめています。

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

**v1.0 前の改善**

- [ ] （ニューラルを配置した場合）編集直後にすぐ書き出しても、ニューラル加工の結果が書き出される（書き出しが合成の完了を待つ。数十秒かかることがある）
- [ ] プロジェクトを開いた直後に書き出しても、編集が反映される（読み込み・レンダリングの完了を待つ）
- [ ] 拍子を 3/4 や 6/8 に変えたプロジェクトで、定規の小節番号とグリッドの小節線が Sonar と合う。MIDI の書き出しに拍子が入る
- [ ] プロジェクトと異なるサンプルレートの素材（例: 44.1 kHz の素材を 48 kHz のプロジェクト）で、再生・書き出しにノイズやこもりがない
- [ ] Sonar で時間範囲を選択すると、ピアノロールがその範囲を表示する（ホストが範囲を通知しない場合は何も起きない）

**MIX 向けの機能（F-22〜F-29）**

- [ ] 「描く」ツールで描いたピッチが再生に反映され、「消す」で元に戻る。描く・消すが 1 回の操作ごとに Undo できる
- [ ] 「補正しない頭の長さ」を伸ばすと、ピッチ中心の補正をしたノートでも、しゃくりが残って聞こえる
- [ ] 「歯擦音のゲイン」を下げると、サ行の子音だけが小さくなる（母音・ブレスは変わらない）。誤検出で別の音が下がらないか
- [ ] 「音量を均す」で、ノートごとの音量の差が小さくなる
- [ ] 「ブレスに適用」で、ノートの後ろのブレス・ノイズが小さくなる
- [ ] Ctrl+C / Ctrl+V（またはツールバーのボタン）で、編集を別のノートへ貼り付けられる（キーがホストに取られる場合はボタンで）
- [ ] 「ずれの表示」を 20〜30 セントにすると、補正していないノートに赤い枠が付き、補正すると消える
- [ ] ダブル・ハモリのクリップで、リードをガイドに選んで「ガイドに合わせる」を実行すると、ピッチがリードに揃う
- [ ] MIX 向けの編集を含むプロジェクトを保存して開き直すと、手描き・各ゲイン・ずれの表示の設定が復元される
- [ ] 以前のバージョン（v3 の保存データ）のプロジェクトを開ける

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

## 10. リリース

配布は GitHub Releases で行います（専用サイトは設けません。ARA の informationURL はリポジトリの URL）。

1. `CMakeLists.txt` の `project(KoeNote VERSION x.y.z)` を更新してコミットします。
2. 同じバージョンのタグを付けて push します。

   ```bash
   git tag v0.1.0
   git push origin v0.1.0
   ```

3. `.github/workflows/release.yml` が、ビルド・単体テストの後に次の 2 つを作り、Releases に下書き（draft）として置きます。
   タグとバージョンが一致しない場合は失敗します。
   - `KoeNote-x.y.z-windows-x64.zip`（`KoeNote.vst3`、ライセンス文、README、`docs/user-guide.md`、`docs/neural-setup.md`）
   - `KoeNote-x.y.z-windows-x64-setup.exe`（Inno Setup。`installer/KoeNote.iss`）
4. 下書きの内容（リリースノート、添付ファイル）を確認して、GitHub の画面で公開します。

- タグを付ける前の試運転は、`gh workflow run release.yml`（または GitHub の Actions 画面の「Run workflow」）で行います。同じ手順で zip とインストーラーを作り、Releases は作らずに Actions の成果物（`KoeNote-x.y.z-windows-x64`）として置きます。
- AGPLv3 のため、配布物に対応するソースが必要です。GitHub Releases はタグのソース（zip / tar.gz）を自動で添付するため、それで満たします。
- インストーラーの定義は、通常の CI（`ci.yml`）でも毎回ビルドして確認しています（成果物 `KoeNote-installer-windows-x64`）。
- ローカルでインストーラーを作る場合は Inno Setup 6 を入れて、`iscc /DAppVersion=x.y.z /DSourceRoot=<ルート> installer\KoeNote.iss` を実行します。
- インストーラーとプラグインには署名がありません（SmartScreen の警告が出る）。署名する場合はコード署名証明書が必要です。
