# KoeNote

歌声の音程とタイミングを、DAW を離れずにノート単位で補正する、無料のボーカル編集プラグインです（Melodyne 型）。
ARA2（Audio Random Access）対応の VST3 として実装しています。

- 対象: 単旋律のボーカル（歌い手、ボカロ系ボーカル）
- 対応 OS: Windows 10/11（x64）。基準 DAW は Cakewalk Sonar（無料版）
- ライセンス: [AGPLv3](LICENSE)。サードパーティの表示は [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)

## 状況

フェーズ2（v1.0 の機能）と MIX 向けの機能まで実装が完了しています。フェーズ0・1 は Cakewalk Sonar で動作確認済みで、フェーズ2 以降は Sonar での確認がこれからです。

| 機能 | 状態 |
|---|---|
| ARA2 連携、ピッチ解析、ピアノロール、ノートのピッチ・タイミング編集、ピッチ曲線の微調整、分割・結合・境界編集、Undo/Redo・状態保存（F-01〜F-08） | 実装済み・Sonar で確認済み |
| フォルマント制御、ノートのゲイン・子音/ブレスの調整（F-09・F-10） | 実装済み |
| MIDI 抽出（DAW へドラッグ&ドロップ）、MIDI をターゲットに適用（F-11・F-12） | 実装済み |
| ガイド音声への DTW 整列、テンポ・グリッド同期、キー・スケール検出、一括補正（F-13〜F-16） | 実装済み |
| ニューラル加工（任意。[セットアップ](docs/neural-setup.md)）、A/B 比較・バイパス（F-17・F-18） | 実装済み |
| MIX 向け: ピッチ曲線の手描き、補正の頭・尻の除外、歯擦音の調整、音量の均し、ブレス・ノイズの一括調整、編集のコピー・貼り付け、ずれの大きいノートの表示、ガイドのピッチへの追従（F-22〜F-29） | 実装済み |
| 非 ARA 環境のキャプチャモード、歌詞・音素表示、ARA 経由のノート提供、マルチトラック表示、プリセット、macOS 対応など（F-19〜F-21、F-30・F-31） | フェーズ3 |

## ダウンロードと使い方

[Releases](https://github.com/shiki-01/KoeNote/releases) から、インストーラー（`*-setup.exe`）または zip をダウンロードします。
インストール、DAW での割り当て、操作方法は [使い方](docs/user-guide.md) を参照してください。

## ビルド

```bash
git clone --recursive https://github.com/shiki-01/KoeNote.git
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

成果物は `build/src/plugin/KoeNote_artefacts/Release/VST3/KoeNote.vst3` です。
配置方法、Cakewalk Sonar での確認手順、操作方法は [docs/dev-setup.md](docs/dev-setup.md) を参照してください。

## ドキュメント

- [使い方（利用者向け）](docs/user-guide.md)
- [要件定義書](docs/requirements.md)
- [設計メモ](docs/design.md)
- [開発環境と動作確認の手順](docs/dev-setup.md)
- [ニューラル加工のセットアップ](docs/neural-setup.md)
