# KoeNote

歌声の音程とタイミングを、DAW を離れずにノート単位で補正する、無料のボーカル編集プラグインです（Melodyne 型）。
ARA2（Audio Random Access）対応の VST3 として実装しています。

- 対象: 単旋律のボーカル（歌い手、ボカロ系ボーカル）
- 対応 OS: Windows 10/11（x64）。基準 DAW は Cakewalk Sonar（無料版）
- ライセンス: [AGPLv3](LICENSE)。サードパーティの表示は [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)

## 状況

フェーズ1（MVP）の実装が完了しています。ARA で取得したクリップを自動で解析し、ピアノロール上でノートのピッチ・タイミングを編集して、
結果を DAW の再生・ミックスダウンへ反映します。DAW 実機（Cakewalk Sonar）での動作確認は、これから行います。

| 機能 | 状態 |
|---|---|
| ARA2 連携、ピッチ解析、ピアノロール、ノートのピッチ・タイミング編集、ピッチ曲線の微調整、分割・結合・境界編集、Undo/Redo・状態保存（F-01〜F-08） | 実装済み |
| フォルマント制御、MIDI 抽出・適用、タイミング整列、キー検出、一括補正、ニューラル加工など（F-09〜F-18） | フェーズ2 |

## ビルド

```bash
git clone --recursive <このリポジトリの URL>
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

成果物は `build/src/plugin/KoeNote_artefacts/Release/VST3/KoeNote.vst3` です。
配置方法、Cakewalk Sonar での確認手順、操作方法は [docs/dev-setup.md](docs/dev-setup.md) を参照してください。

## ドキュメント

- [要件定義書](docs/requirements.md)
- [設計メモ](docs/design.md)
- [開発環境と動作確認の手順](docs/dev-setup.md)
