# ニューラル加工（F-17）のセットアップ

KoeNote は、ニューラルボコーダー（NSF-HiFiGAN 系）による高品質な合成を、**任意の追加機能**として使えます。
ONNX Runtime とボコーダーのモデルは、ライセンス上の理由（モデルの重みに非商用条件があるため）で本体には同梱していません。
利用者が条件を確認したうえで、次の手順で配置します。

## 1. 必要なもの

| 項目 | 内容 | ライセンス |
|---|---|---|
| ONNX Runtime | `onnxruntime.dll`（Windows x64 の CPU 版。動作確認は v1.31.0）。https://github.com/microsoft/onnxruntime/releases の `onnxruntime-win-x64-*.zip` の `lib/` にある `onnxruntime.dll` と `onnxruntime_providers_shared.dll` | MIT |
| ボコーダー | 例: DiffSinger Community Vocoder の **PC-NSF-HiFiGAN**（44.1 kHz、hop 512、128 bin、2025.02）。https://github.com/openvpi/vocoders/releases の `pc_nsf_hifigan_44.1k_hop512_128bin_2025.02.oudep` | CC BY-NC-SA 4.0（**非商用**・継承） |

- `.oudep` は zip です。拡張子を `.zip` にして展開すると、`vocoder.yaml`、`*.onnx`、`NOTICE.txt` などが入っています。
- PC-NSF-HiFiGAN の重みは CC BY-NC-SA 4.0 です。**商用利用はできません**。生成した音声の扱いは、`NOTICE.txt` の条件に従ってください。
- ほかの NSF-HiFiGAN 系（`nsf-hifigan` の ONNX など）も、`vocoder.yaml` の項目（sample_rate、hop_size、win_size、fft_size、num_mel_bins、mel_fmin、mel_fmax、model）が同じ形式であれば使える見込みです（未検証）。入力は `mel`（[1, フレーム, bin]）と `f0`（[1, フレーム]）、出力は波形を想定しています。

## 2. 配置

次のいずれかのフォルダに、`vocoder.yaml`、`.onnx`、`onnxruntime.dll`、`onnxruntime_providers_shared.dll` を置きます。

1. `%APPDATA%\KoeNote\neural\`（直下、またはその 1 階層下のフォルダ。展開したフォルダごと置いてもよい）
2. プラグイン本体（`KoeNote.vst3\Contents\x86_64-win\`）と同じフォルダの `neural\`
3. 環境変数 `KOENOTE_NEURAL_DIR` が指すフォルダ（開発・検証用）

`onnxruntime.dll` は、`vocoder.yaml` と同じフォルダ、その下の `runtime\`、または親フォルダのいずれかにあれば見つかります。
配置の例:

```
%APPDATA%\KoeNote\neural\
    onnxruntime.dll
    onnxruntime_providers_shared.dll
    pc_nsf_hifigan_44.1k_hop512_128bin_2025.02\
        vocoder.yaml
        pc_nsf_hifigan_44.1k_hop512_128bin_2025.02.onnx
        NOTICE.txt
```

Windows 標準の `onnxruntime.dll`（System32）と取り違えないよう、KoeNote は上記の場所にある DLL を絶対パスで読み込みます。

## 3. 使い方

1. Editor の右側のタブ「その他」→「ニューラル加工（高品質）」で「ニューラルボコーダーで合成する」を有効にします。
2. 配置が見つかっていれば有効にできます。見つからない場合は、理由（例: `vocoder package not found`）と配置先のフォルダが表示されます。
3. ノートを編集すると、まず標準方式（TD-PSOLA）の結果が再生に反映されます。編集が約 0.5 秒止まると、ニューラルの合成が始まります
   （ステータスに「合成中… n%」）。完了すると、再生が自動的にニューラルの結果へ切り替わります。
4. 合成中に別の編集をすると、実行中の合成は取り消され、最新の編集に対して合成し直します。切り替わるまでの間も音は途切れません。
5. **ミックスダウン（書き出し）の前に、ステータスが「反映済み」になっていることを確認してください。**
   合成が終わっていない場合、書き出しには標準方式（TD-PSOLA）の結果が使われます。

## 4. 仕様と制約

- 合成はモノラルです。ステレオの素材は、左右同じ波形になります（ステレオの定位は失われます）。
- 編集のないクリップには適用しません（元の音声をそのまま使います）。
- 処理は CPU で行います（GPU は使いません）。動作確認した環境（8 コア程度の PC）では、3 秒の音声を約 0.5 秒で合成しました
  （実時間の約 6 倍）。3 分のボーカルで 30 秒前後が目安です。
- フォルマントのシフトは、メルの周波数軸をずらして行います（±1 オクターブまで）。
- 合成に使うメモリは、元音声・レンダリング結果に加えて、ボコーダー（約 60 MB）と処理中の配列です。

## 5. トラブルシューティング

| 表示 | 原因と対処 |
|---|---|
| `vocoder package not found` | 配置先に `vocoder.yaml` がない。2 章の場所を確認する |
| `model file not found: ...` | `vocoder.yaml` の `model:` が指す `.onnx` が同じフォルダにない |
| `onnxruntime.dll not found next to the vocoder` | `onnxruntime.dll` が見つからない。`vocoder.yaml` と同じフォルダに置く |
| `LoadLibrary failed ...` | DLL が壊れている、または 64 bit 版でない。`onnxruntime_providers_shared.dll` も同じフォルダに置く |
| `unsupported onnxruntime version` | `onnxruntime.dll` が古い、または新しすぎる。動作確認は v1.31.0 のみ |
| モデルのエラー（入力の形など） | NSF-HiFiGAN 系の入力（mel, f0）と異なるモデル。1 章を参照 |
