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

1. オーディオトラックにボーカルのクリップを配置します（モノラル・ステレオのどちらでも可。プロジェクトのサンプルレートと同じ素材を使ってください。フェーズ0〜1 ではサンプルレート変換は未対応です）。
2. 対象のクリップを選択し、右クリックまたはクリップのメニューから ARA 関連のコマンド（例: 「Audio Random Access」→「KoeNote」）を選びます。
   Sonar のバージョンによってメニュー名が異なる場合があります。ARA 対応プラグインはトラックの FX ビンではなく、
   クリップ（Region）に対して割り当てる点が通常のエフェクトと異なります。
3. 割り当てると、KoeNote の Editor が開きます。ARA に接続されている場合は「KoeNote (ARA 接続中)」と表示されます
   （フェーズ1 以降は波形とピアノロールが表示されます）。
4. 再生して、元のクリップと同じ音が聞こえることを確認します（フェーズ0 の完了条件: パススルー）。

### 4.3 確認項目（フェーズ0）

- [ ] Sonar が KoeNote を VST3 としてスキャンできる
- [ ] クリップへ割り当てると Editor が開き、「ARA 接続中」と表示される
- [ ] 再生で元の音声が欠けずに聞こえる（再生開始直後の数十ミリ秒が無音になる場合は、読み込み完了前の可能性があるため報告してください）
- [ ] クリップを移動・トリムしても再生が追従する
- [ ] プロジェクトを保存して開き直しても、クリップへの割り当てが保持される
- [ ] ミックスダウン（書き出し）で、再生時と同じ音声が得られる

不具合があれば、Sonar のバージョン、素材のサンプルレート・チャンネル数、再現手順を記録してください。

## 5. pluginval による検査

CI と同じ条件でローカル検査する場合（pluginval を別途ダウンロードしておく）:

```bash
pluginval --strictness-level 5 --validate build/src/plugin/KoeNote_artefacts/Release/VST3/KoeNote.vst3
```

## 6. 精度評価（mir_eval）

`tools/eval/` を参照してください（Python 3.10 以降、`pip install -r tools/eval/requirements.txt`）。

## 7. ライセンス上の注意

- JUCE は AGPLv3 を選択しています。ソース公開が前提のため、ビルドした VST3 を配布する場合は対応するソースも提供してください。
- 学習済みモデルの重みはリポジトリに含めません（`.gitignore` で除外）。
