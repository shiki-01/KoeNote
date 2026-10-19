# Third-Party Notices

KoeNote は GNU Affero General Public License v3.0（`LICENSE`）で配布します。
以下は、本リポジトリが利用・同梱するサードパーティソフトウェアの著作権表示とライセンスです。
各ライブラリの完全なライセンス文は、`external/` 以下の各 submodule、およびビルド成果物に同梱される場合は
そちらを参照してください。

## JUCE

- 用途: プラグインフレームワーク（`external/JUCE`、バージョン 8.0.15）
- 著作権: Copyright (c) Raw Material Software Limited
- ライセンス: AGPLv3（本プロジェクトは JUCE の AGPLv3 ライセンスを選択して使用します）
- https://github.com/juce-framework/JUCE

JUCE に含まれる各モジュールは、それぞれの著作権表示とライセンス（ISC、BSD、MIT など）を持ちます。
詳細は `external/JUCE/modules/*/` 内の各ファイルのヘッダ、および JUCE の `LICENSE.md` を参照してください。

## ARA SDK

- 用途: Audio Random Access（ARA2）の plug-in 側実装（`external/ARA_SDK`、releases/2.3.0）
- 著作権: Copyright (c) 2012-2025, Celemony Software GmbH
- ライセンス: Apache License 2.0
- https://github.com/Celemony/ARA_SDK

> Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in
> compliance with the License. You may obtain a copy of the License at
> http://www.apache.org/licenses/LICENSE-2.0

## VST3 SDK

- 用途: VST3 プラグイン形式（JUCE が `modules/juce_audio_processors_headless/format_types/VST3_SDK` に同梱）
- 著作権: Copyright (c) 2025, Steinberg Media Technologies GmbH
- ライセンス: MIT License（VST3 SDK 3.8 以降）
- https://github.com/steinbergmedia/vst3sdk

> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
> associated documentation files (the "Software"), to deal in the Software without restriction, including
> without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
> following conditions: The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.

VST は Steinberg Media Technologies GmbH の登録商標です。

## Catch2（テストのみ。配布物には含まれない）

- 用途: 単体テスト（`external/Catch2`、v3.16.1）
- 著作権: Catch2 contributors（詳細は `external/Catch2/LICENSE.txt` と各ソースのヘッダを参照）
- ライセンス: Boost Software License 1.0
- https://github.com/catchorg/Catch2

## 学習済みモデル

現時点でモデルの重みは同梱していません。将来モデルを同梱・利用する場合は、ここにモデル名・配布元・ライセンスを追記します。
非商用条件付きの重み（例: PC-NSF-HiFiGAN の公開重み、CC BY-NC-SA 4.0）は本体に同梱せず、利用者が任意で追加する方式とします。
