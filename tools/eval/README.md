# tools/eval — 精度評価

mir_eval でピッチ推定（RPA・オクターブ誤り）とノート分割（COnP F 値）を評価します。
目標値は要件定義書 4章を参照してください。

評価の入力 CSV は、リポジトリの `koenote_analyze`（`tools/analyze`）で作れます。

```bash
koenote_analyze analyze vocal.wav out/                 # out/vocal_pitch.csv, out/vocal_notes.csv
koenote_analyze midi vocal_ref.mid out/ref_notes.csv    # 正解ノートの CSV
```

```bash
pip install -r requirements.txt
python evaluate.py --ref-pitch data/ref_pitch.csv --est-pitch out/est_pitch.csv
python evaluate.py --ref-notes data/ref_notes.csv --est-notes out/est_notes.csv
```

評価用データ（本人の歌声の録音、公開データセット等）は `tools/eval/data/` に置きます。
配布物・リポジトリには含めません（`.gitignore` で除外済み）。
正解ノートは、録音時に作成した MIDI（歌った旋律）から作成します（要件定義書 11章）。

## 一括評価

`tools/eval/data/` に `<名前>.wav`（歌声）と `<名前>.mid`（録音時に作成した正解の旋律）の組を置き、次を実行すると、全ての組を評価します。

```bash
powershell -ExecutionPolicy Bypass -File tools/eval/run_all.ps1
```

- `-DataDir` で別のフォルダ、`-Python py` で Python の起動コマンドを指定できます。結果の CSV は `tools/eval/out/` に出力されます。
- 評価用データはリポジトリに含めないため、CI では実行しません（合成歌声での評価は単体テスト `test_accuracy.cpp` が CI で行います）。
