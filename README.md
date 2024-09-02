﻿Subtitler.auf
=============

Subtitler.auf は音声からの自動文字起こしを行うための AviUtl プラグインです。

Subtitler.auf の動作には AviUtl version 1.00 以降と拡張編集 version 0.92 が必要です。

注意事項
-------

Subtitler.auf は無保証で提供されます。  
Subtitler.auf を使用したこと及び使用しなかったことによるいかなる損害について、開発者は責任を負いません。

これに同意できない場合、あなたは Subtitler.auf を使用することができません。

ダウンロード
------------

https://github.com/oov/aviutl_subtitler/releases

インストール／アンインストール
------------------------------

Subtitler.auf と Subtitler フォルダーを **exedit.auf と同じ場所** に置いてください。

アンインストールは Subtitler.auf と Subtitler フォルダーを削除すれば完了です。

ただし、実際に使用するためには以下で説明する Whisper の準備が必要です。

### Whisper の準備

Subtitler.auf は音声の文字起こしに Whisper を使用します。  
Whisper だけではなく、より高速な Faster-Whisper も使用できます。  
（コマンドライン引数にある程度の互換性があることが前提です）

Faster-Whisper の Windows 用コンパイル済みプログラムは以下からダウンロードできます。

https://github.com/Purfview/whisper-standalone-win/releases

少し下にスクロールすると `Faster-Whisper` があります。  
(`Faster-Whisper-XXL` でも動きますが、一時ディレクトリーにゴミファイルが残ったりするのであまりオススメしません)

また、`cuBLAS and cuDNN 8.x libs` のリンクから対応する DLL をダウンロードし、`Faster-Whisper` と同じフォルダーに置いてください。

使い方
------

### 1. Subtitler ウィンドウの表示

プラグインを導入した状態で AviUtl を起動すると、  
`表示` メニューから `Subtitlerの表示` を選択できるようになります。

これを選ぶことで設定ウィンドウが表示されます。

### 2. 全体設定をする

ウィンドウが開いたら、まずは `Whisper の実行可能ファイルへのパス` を設定してください。  
これを設定しなければ他の設定はすべて触れません。

また、こちらは省略もできますが、`モデルの配置場所` も指定しておくことを推奨します。  
この場所には数GBあるようなモデルファイルが配置されるため、空き容量に気をつけてください。

### 3. 字幕生成を行う

`字幕生成` タブで `処理開始` ボタンを押すと、現在の選択範囲に対して音声の文字起こしを行います。

なお、大きな範囲に対して処理を行うと時間も掛かるため、適度な範囲で処理してください。  
またバグなどのトラブルによるデータ消失などを防ぐために、処理前には保存しておくことを推奨します。

設定について
------------

### 字幕生成タブ

- **モデル**
  - Whisper で使用するモデルを選択します。  
    小さいモデルを選択すると処理速度は速くなりますが、精度が低くなるため注意してください。  
    `distil-` が付いているモデルは英語のみの対応です。  
    処理速度に問題がければ large-v3 を選んでおくのが無難です。
- **言語**
  - 音声の言語を選択します。  
    選択した言語に応じて音声の文字起こしが行われます。  
    空欄にすると自動判定されますが、判定のための処理が加わるため余計に時間が掛かります。
- **初期プロンプト**
  - 音声の文字起こしの際に使われるヒントのようなものです。  
    例えば「音声入力」のはずが「温泉入浴」と誤って文字起こしされているときに、  
    初期プロンプトに `音声入力` と入れておくと結果を改善できる場合があります。  
    ChatGPT のように対話式に入力する必要はありません。
- **挿入位置**
  - 生成された字幕をどのレイヤーに挿入するかを選びます。  
    挿入先に既に別のオブジェクトがある場合はその下に挿入されます。
- **挿入モード**
  - 挿入先に既にオブジェクトがある場合の挙動を選びます。  
    `最初に見つかった空き` の場合は、途中に隙間があればそこに挿入されます。  
    `最後に見つかった空き` の場合は、下にオブジェクトがない位置に挿入されます。
- **モジュール**
  - 拡張編集にどうやってオブジェクトを作るのかを選択します。  
    これは Subtitler フォルダーにある Lua スクリプトによって実現されています。  
    自分でスクリプトを書くことで動作をカスタマイズできます。
- **処理開始**
  - 字幕の生成処理を開始します。

### 全体設定タブ

- **Whisper の実行可能ファイルへのパス**
  - Whisper の実行ファイルのパスを指定します。  
    これを設定しないと文字起こしが行えません。
- **モデルの配置場所**
  - Whisper が使うモデルファイルの置き場所を指定します。  
    これを指定しなくても動きますが、使うプログラムによってデフォルトの配置場所が異なります。  
    巨大なファイルが散らばるのを防ぐためにも設定しておくことを推奨します。
- **Whisper への追加のパラメーター**
  - プログラムに追加のパラメーターを渡せます。  
    どんなパラメーターが使えるのかは使用するコマンドのドキュメントなどを参照してください。

### 高度タブ

Subtitler では内部的に3ステップの処理を行っており、これを個別に実行できます。

例えば Lua スクリプトを自作する場合は予めステップ1と2を実行しておき、  
Lua スクリプトを編集するたびにステップ3だけを繰り返し実行したりすれば効率的に開発作業が行えます。

この際に作成されるファイルは一時フォルダーに残るため、必要に応じて手動削除してください。  
また、ファイル名はAviUtlを終了しても変わらないので、手動で消すまでは何度でも使い回せます。

よくある質問
------------

- 動かない
  - 情報提供が下手すぎる。  
    近くの詳しい人にでも聞いてみてください。
- 動かないので直しました
  - 情報提供が上手すぎる。  
    修正内容をプルリクエストで送ってくれたらありがたいです。
- `Whisper の実行可能ファイルへのパス` が入力できない
  - インストール方法のところを読みながらインストールし直してください。
- 処理終了時に鳴る音がうるさい
  - 音が鳴るのは Purfview 版 Faster-Whisper と Faster-Whisper-XXL の独自処理です。  
    `--beep_off` のオプションを付けて実行すると音が鳴らなくなります。

ビルドについて
--------------

Windows 上の Git Bash などで `bash build.bash` でビルドできます。

Credits
-------

Subtitler.auf is made possible by the following open source softwares.

### [Acutest](https://github.com/mity/acutest)

<details>
<summary>The MIT License</summary>

```
The MIT License (MIT)

Copyright © 2013-2019 Martin Mitáš

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the “Software”),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.
```
</details>

### [AviUtl Plugin SDK](http://spring-fragrance.mints.ne.jp/aviutl/)

<details>
<summary>The MIT License</summary>

```
The MIT License

Copyright (c) 1999-2012 Kenkun

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```
</details>

### [hashmap.c](https://github.com/tidwall/hashmap.c)

NOTICE: This program used [a modified version of hashmap.c](https://github.com/oov/hashmap.c/tree/simplify).

<details>
<summary>The MIT License</summary>

```
The MIT License (MIT)

Copyright (c) 2020 Joshua J Baker

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
</details>

### [nanoprintf](https://github.com/charlesnicholson/nanoprintf)

NOTICE: This program used [a modified version of nanoprintf](https://github.com/oov/ovbase/blob/main/src/printf/nanoprintf.h).

<details>
<summary>UNLICENSE</summary>

```
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org>
```
</details>

### [libopusenc](https://opus-codec.org/)

<details>
<summary>The 3-Clause BSD License</summary>

```
Copyright (c) 1994-2013 Xiph.Org Foundation and contributors
Copyright (c) 2017 Jean-Marc Valin

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of the Xiph.Org Foundation nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
</details>

### [Ogg](https://xiph.org/ogg/)

<details>
<summary>The 3-Clause BSD License</summary>

```
Copyright (c) 2002, Xiph.org Foundation

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of the Xiph.org Foundation nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
</details>

### [Opus](https://opus-codec.org/)

<details>
<summary>The 3-Clause BSD License</summary>

```
Copyright 2001-2023 Xiph.Org, Skype Limited, Octasic,
                    Jean-Marc Valin, Timothy B. Terriberry,
                    CSIRO, Gregory Maxwell, Mark Borgerding,
                    Erik de Castro Lopo, Mozilla, Amazon

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of Internet Society, IETF or IETF Trust, nor the
names of specific contributors, may be used to endorse or promote
products derived from this software without specific prior written
permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
</details>

### [Opusfile](https://opus-codec.org/)

<details>
<summary>The 3-Clause BSD License</summary>

```
Copyright (c) 1994-2013 Xiph.Org Foundation and contributors

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of the Xiph.Org Foundation nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
</details>

### [TinyCThread](https://github.com/tinycthread/tinycthread)

NOTICE: This program used [a modified version of TinyCThread](https://github.com/oov/tinycthread).

<details>
<summary>The zlib/libpng License</summary>

```
Copyright (c) 2012 Marcus Geelnard
              2013-2016 Evan Nemerson

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software
    in a product, an acknowledgment in the product documentation would be
    appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.

    3. This notice may not be removed or altered from any source
    distribution.
```
</details>

### [yyjson](https://github.com/ibireme/yyjson)

<details>
<summary>MIT License</summary>

```
MIT License

Copyright (c) 2020 YaoYuan <ibireme@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
</details>

## 更新履歴

CHANGELOG を参照してください。

https://github.com/oov/aviutl_subtitler/blob/main/CHANGELOG.md