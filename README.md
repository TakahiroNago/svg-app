
# SVG App

C++とOpenCVを使用した、ラスタ画像からSVGベクタ画像への変換ツールです。

## 概要

PNGなどのラスタ画像を解析し、輪郭情報を抽出してSVG形式のベクタ画像に変換することを目指しています。

まずは、白黒のロゴやイラストを対象とした変換機能の実装を進めています。

## 使用技術

- C++20
- OpenCV
- CMake
- MSVC
- vcpkg

## 実装済み機能

- OpenCVでカラー画像を読み込み、Lab色空間のk-meansで減色
- 抽出した領域輪郭のSVG出力（穴のある図形に対応）
- `--colors` で色数（2, 3, 4, 5, 6, 8, 12, 16, 24, 32）を指定
- パレットを表示し、SVGに含める色を対話的に選択
- `--boundary` による領域の塗りつぶしなしの境界線SVG出力

## 変換処理の構成

画像解析とSVG生成は `src/converter.hpp` / `src/converter.cpp` の `svg-converter` ライブラリに分離されています。CLIはファイル入出力と色選択の対話を担当し、変換コアはOpenCVのBGR画像を受け取ってパレット解析とSVG文字列の生成を行います。

- `svg_app::analyzeImage`：色数に従って画像を量子化し、パレット、領域面積、描画順を返します。
- `svg_app::generateSvg`：解析結果と境界線設定・残す色のインデックスからSVG文字列を生成します。

この分離により、将来Web UIやWebAssemblyから変換コアを呼び出せます。WASM用のOpenCVビルドとブラウザー接続はまだ実装していません。

## ビルド方法

### 必要な環境

- Windows
- Visual Studio Build Tools（C++開発環境）
- CMake
- vcpkg
- OpenCV

### ビルド手順

プロジェクトのルートディレクトリで、以下のコマンドを実行します。

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
```

続いて、Release構成でビルドします。

```powershell
cmake --build build --config Release
```

ビルドが成功すると、実行ファイルが `build/Release/svg-app.exe` に生成されます。

## 使い方

入力画像と出力するSVGファイルのパスを指定して実行します。

```powershell
.\build\Release\svg-app.exe .\samples\input.png .\output.svg
```

抽出した領域は量子化された色で塗りつぶされ、背景は透明なSVGとして保存されます。パレット表示後、残す色を選択できます。入力を空にするとすべての色を使用します。

色数を指定するには `--colors` を使います。

```powershell
.\build\Release\svg-app.exe .\samples\input.png .\output.svg --colors 8
```

境界線のみをSVGに出力するには、`--boundary` を指定します。

```powershell
.\build\Release\svg-app.exe .\samples\input.png .\output-boundary.svg --boundary
```

このモードでは量子化された各領域の輪郭をその領域の色で描画し、塗りつぶしは行いません。画像全体を囲む外枠は出力しません。

## 今後の開発予定

- 白黒ロゴやイラストの変換品質の改善
- サンプル画像を使用した動作確認
- 自動テストの導入

## ライセンス

ライセンスは今後設定する予定です。
