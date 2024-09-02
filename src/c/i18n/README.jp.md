# 翻訳作業をされる方へ

このディレクトリーには、各言語用の翻訳ファイルが配置されています。

注意: この文書はこのプロジェクトに固有のものではありません。  
途中で登場する URL やプログラム名などには架空のものを使用していますので、適宜読み替えてください。

## 更新作業で必要になるツール

翻訳作業はプログラムのコンパイル環境がなくても行えます。

- Git for Windows  
  https://gitforwindows.org/  
  実際には `bash`, `curl`, `gettext` などがあれば Linux 上でも作業は可能です。
- Poedit  
  https://poedit.net/  
  なくても構いませんが、あると作業しやすいでしょう。

## 更新作業で扱うファイル

翻訳ファイルの更新作業では、以下のファイルを利用します。  
**人間が手作業で編集するのは `*.po` のみ**です。

- `*.pot` ファイル
  - ソースコードから翻訳すべき部分を抽出したテンプレートファイルです。
  - このファイルは自動生成できるため、リポジトリーでは管理されていません。
  - これは人間が直接編集するためのファイルではありません。
- `*.po.DO_NOT_EDIT` ファイル
  - 現在の翻訳データが保持されている、各言語ごとのファイルです。
  - このファイルはリポジトリーで管理されています。
  - これは人間が直接編集するためのファイルではありません。
- `*.po` ファイル
  - 人間が翻訳作業を行うためのファイルです。
  - `*.pot` と `*.po.DO_NOT_EDIT` からこのファイルが生成されます。
  - 編集後、このファイルを元に `*.po.DO_NOT_EDIT` が更新されます。
- `*.mo` ファイル
  - `*.po.DO_NOT_EDIT` をコンパイルしたもので、プログラムに組み込むファイルです。
  - このファイルは自動生成できるため、リポジトリーでは管理されていません。
  - これは人間が直接編集するためのファイルではありません。

## 更新作業の流れ

更新作業は以下のような流れで行います。  
ステップ数が多いですが、実際には複数のステップが一つのコマンドで完了します。

1. リポジトリーを clone する
    - 作業を行うために必要です。
2. `*.pot` ファイルを生成する
    - 現在のソースコードに対応した翻訳ファイルを作成するために必要です。
3. `*.po` を生成する
    - 用意した `*.pot` と `*.po.DO_NOT_EDIT` を元に作業用ファイルを作成します。
    - もし `*.po.DO_NOT_EDIT` がない場合は `*.po` を新規作成されます。
4. `*.po` を編集する
    - テキストエディターだけでも編集できますし、[Poedit](https://poedit.net/) なども利用できます。
5. 新しい `*.po.DO_NOT_EDIT` を生成する
    - 編集した `*.po` ファイルを元に `*.po.DO_NOT_EDIT` を生成します。
6. `*.mo` を生成する
    - `*.po.DO_NOT_EDIT` を使って `*.mo` を生成します。
7. プログラムに `*.mo` を組み込む
    - `*.mo` を実際に組み込んで動作テストをします。
8. リポジトリーに `*.po.DO_NOT_EDIT` をコミットする
    - これで更新作業が完了します。
    - 変更をプルリクエストしてくれると嬉しいです。

## 1. リポジトリーを clone する

作業を行うために、プロジェクトのリポジトリーを clone します。  
最終的にプルリクエストを行う場合は fork して、自身のリポジトリーを使うといいでしょう。

```bash
git clone https://github.com/user/project
```

## 2. `*.pot` ファイルを生成する / 3. `*.po` を生成する

2 と 3 の手順は、スクリプトを実行するだけで自動的に完了します。  
このディレクトリーを Git BASH で開いて、以下のコマンドを実行します。

```bash
bash start.bash
```

これで現在のソースコードに対する翻訳作業用ファイルが作成されます。  
ただし、既に `*.po` がある場合はファイル生成は行われません。  
作業中のファイルがある場合は、すべて削除してからスクリプトを実行し直してください。

## 4. `*.po` を編集する

これは GNU gettext の `*.po` ファイルです。  
通常と同じように翻訳作業を進めてください。

テキストエディターだけでも編集できますし、[Poedit](https://poedit.net/) なども利用できます。

## 5. 新しい `*.po.DO_NOT_EDIT` を生成する / 6. `*.mo` を生成する / 7. プログラムに `*.mo` を組み込む

5 と 6 と 7 の手順は、スクリプトを実行するだけで自動的に完了します。

編集が終わったら Git BASH でこのディレクトリーを開き、以下のコマンドを実行します。

```bash
bash finish.bash
```

これで変更が適用された `*.po.DO_NOT_EDIT` と `*.mo` が一気に生成されます。

もし翻訳データをコンパイル済みプログラムへ組み込みたい場合は、実行可能形式ファイルへのパスを渡します。

```bash
# 同じフォルダー内に program.exe がある場合
bash finish.bash program.exe
```

これで `program.exe` に新しい翻訳データが書き込まれます。

なお、翻訳データの埋め込みには Windows の標準的な仕組みを利用しています。  
[Resource Hacker](http://www.angusj.com/resourcehacker/) などで差し替えることでも変更を適用できますが、この作業をコマンドラインから簡単に行えるようにするために、[minirh](https://github.com/oov/minirh) という簡単なオープンソースプログラムを作りました。  
`finish.bash` では minirh を使用しており、まだ minirh が存在しなければ自動的にダウンロードされ、`.tool` ディレクトリーに配置した上で利用されます。

## 8. リポジトリーに `*.po.DO_NOT_EDIT` をコミットする

あなたの作業内容をリポジトリーにコミットする場合は `*.po.DO_NOT_EDIT` をコミットしてください。  
このファイルは変更点が最小限になるよう加工が行われています。