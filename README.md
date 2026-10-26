
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

- 画像のグレースケール読み込み
- 大津の二値化アルゴリズムによる二値画像への変換
- OpenCVによる輪郭抽出
- 抽出した輪郭のSVGファイルへの保存（穴のある図形に対応）
- `--boundary` オプションによる、二値化した2領域の境界線のみのSVG出力

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

抽出した図形は黒で塗りつぶされ、背景は透明なSVGとして保存されます。

境界線のみをSVGに出力するには、`--boundary` を指定します。

```powershell
.\build\Release\svg-app.exe .\samples\input.png .\output-boundary.svg --boundary
```

このモードでは大津の二値化で分けた領域の輪郭を黒い線で描画し、領域の塗りつぶしは行いません。画像全体を囲む外枠は出力しません。現在の入力処理はグレースケール化と二値化を行うため、カラー画像も明暗に基づいて2領域に分けられます。

## 今後の開発予定

- 白黒ロゴやイラストの変換品質の改善
- サンプル画像を使用した動作確認
- 自動テストの導入

## ライセンス

ライセンスは今後設定する予定です。
