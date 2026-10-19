# tools/eval — 精度評価

mir_eval でピッチ推定（RPA・オクターブ誤り）とノート分割（COnP F 値）を評価します。
目標値は要件定義書 4章を参照してください。

```bash
pip install -r requirements.txt
python evaluate.py --ref-pitch data/ref_pitch.csv --est-pitch out/est_pitch.csv
python evaluate.py --ref-notes data/ref_notes.csv --est-notes out/est_notes.csv
```

評価用データ（本人の歌声の録音、公開データセット等）は `tools/eval/data/` に置きます。
配布物・リポジトリには含めません（`.gitignore` で除外済み）。
正解ノートは、録音時に作成した MIDI（歌った旋律）から作成します（要件定義書 11章）。
