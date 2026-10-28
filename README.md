# SVG App

C++とOpenCVを使用した、ラスター画像からSVG形式のベクター画像を生成する変換ツールです。

## 概要

PNGなどのラスター画像を解析し、色の領域と輪郭を抽出してSVG形式に変換します。

色数を指定した減色処理に対応しており、抽出した領域を塗りつぶすモードと、境界線のみを描画するモードを利用できます。

## 使用技術

- C++20
- OpenCV
- CMake
- MSVC
- vcpkg

## 実装済み機能

- OpenCVによるカラー画像の読み込み
- Lab色空間でのk-meansによる減色処理
- 色領域の輪郭抽出とSVG出力
- 穴のある図形に対応した輪郭の出力
- `--colors` による色数の指定（2、3、4、5、6、8、12、16、24、32）
- パレットの表示と、SVGに含める色の対話的な選択
- `--boundary` による境界線のみのSVG出力
- 変換処理とCLIの分離

## 変換処理の構成

画像解析とSVG生成の処理は、`src/converter.hpp` / `src/converter.cpp` に実装された `svg-converter` ライブラリに分離しています。

CLIはファイル入出力と色選択の対話処理を担当し、変換コアはOpenCVのBGR画像などの入力を受け取り、画像解析結果やSVG文字列を生成します。

主な関数は次のとおりです。

- `svg_app::analyzeImage`：指定された色数に応じて画像を量子化し、パレット、領域面積、描画順などの解析結果を返します。
- `svg_app::generateSvg`：解析結果、境界線モード、出力対象の色インデックスに基づいてSVG文字列を生成します。

変換コアをCLIから分離することで、Web UIなどの別のインターフェースからも利用できる構成にしています。Web版では、この変換コアをWebAssembly経由で利用します。

## ビルド環境

以下の環境を使用します。

- Windows
- Visual Studio Build Tools（C++開発環境）
- CMake
- vcpkg
- OpenCV

## ビルド方法

プロジェクトのルートディレクトリで実行します。

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
```

Release構成でビルドします。

```powershell
cmake --build build --config Release
```

ビルドが成功すると、実行ファイルが `build/Release/svg-app.exe` に生成されます。

## 使い方

### 基本的な変換

入力画像と出力先のSVGファイルを指定します。

```powershell
.\build\Release\svg-app.exe .\samples\input.png .\output.svg
```

画像を解析してパレットを表示し、SVGに含める色を選択します。入力を空にすると、すべての色を使用します。

生成されるSVGでは、抽出した領域が量子化された色で塗りつぶされ、背景は透明になります。

### 色数の指定

`--colors` で変換後の色数を指定します。

```powershell
.\build\Release\svg-app.exe .\samples\input.png .\output.svg --colors 8
```

### 境界線のみの出力

`--boundary` を指定すると、塗りつぶしを行わず、各領域の輪郭をその領域の色で描画します。

```powershell
.\build\Release\svg-app.exe .\samples\input.png .\output-boundary.svg --boundary
```

このモードでは、画像全体を囲む外枠は出力しません。

## Web版

本プロジェクトの変換コアをWebAssembly経由で利用するWebアプリも開発しています。

- 公開デモ：https://svg.art-full-stack.com/
- フロントエンド：React / TypeScript
- 変換処理：C++ / OpenCV / WebAssembly

## バックエンド（Go API + gRPC）

ブラウザ内WASM変換とは別に、サーバー経由の変換経路を用意しています。

```
React (任意) → HTTP/REST → Go API (api/) → gRPC → C++ エンジン (server/)
```

- `proto/svgconv/v1/svgconv.proto`：gRPC定義（`Convert`）。画像は非乗算RGBAで送ります（WASM版と同じ契約）。
- `server/grpc_server.cpp`：`svg-converter` ライブラリを呼び出すC++ gRPCサーバー（`-DSVG_BUILD_SERVER=ON`）。
- `src/engine_common.*`：WASMとサーバーで共有する入力検証とRGBA→BGR変換。
- `api/`：Go REST API（`POST /api/v1/convert`、`GET /healthz`）。PNG/JPEGのデコードと入力制限を担当します。

### ローカル起動（Docker Compose）

```bash
docker compose up -d --build        # C++エンジン(:50051) と API(:8080) を起動
curl -F file=@samples/logo.png -F colors=8 http://localhost:8080/api/v1/convert
docker compose down
```

### テスト

```bash
bash scripts/gen-proto.sh                                    # proto の lint と Go コード生成
bash scripts/go.sh go test -race ./...                       # Go 単体テスト
docker build --target test -f docker/cpp-engine.Dockerfile . # C++ 単体テスト
bash scripts/integration-test.sh                             # 統合テスト（要 docker compose up）
node --test tests/wasm.test.mjs                              # WASMテスト
node --test tests/e2e/parity.test.mjs                        # WASMとgRPCエンジンの出力一致確認
```

GitHub Actions（`.github/workflows/ci.yml`）で proto、Go、C++、Compose統合テストを実行します。WASMテストとパリティテストはEmscripten環境が必要なため、ローカル実行です。

## 今後の開発予定

- 白黒ロゴやイラストの変換品質の改善
- サンプル画像を使用した変換結果の検証
- 自動テストの導入

## ライセンス

現在、ライセンスは未設定です。利用・再配布条件を明確にするため、今後設定する予定です。