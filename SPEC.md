# esp32-devkitc-firmware 設計仕様書 (SPEC)

## 1. 目的
ESP32-DevKitC 用のファームウェアを GitHub 上で管理・自動ビルドし、GitHub Releases に `.bin` ファイルを公開することで、PC を使わずに専用 Android アプリ（`yuushi-labs/esp32-devkitc-flasher`）から直接 ESP32-DevKitC へ書き込めるようにする。

## 2. ハードウェア仕様
- ボード: **ESP32-DevKitC**（PlatformIO ボード ID: `esp32dev`）
- MCU: ESP32（Xtensa Dual-Core 32-bit LX6）
- フラッシュサイズ: 4MB（標準）、Flash Mode: `dio`、Flash Freq: `40m`
- オンボード LED: GPIO2（ボードにより未実装の場合はシリアル出力で動作識別）
- UART ボーレート: `115200`

## 3. リポジトリ構成
```
.
├── platformio.ini
├── pins.json                      # GPIO 配線の定義（唯一の情報源）
├── src/
│   ├── main.cpp
│   ├── pinout.h / .cpp            # pins.json から生成した配線表の表示
│   └── command_handler.h / .cpp   # シリアルコマンド解析（単体テスト可能に分離）
├── test/                          # PlatformIO native テスト（コマンド解析・フォーマット・配線表示）
├── tests/python/                  # pins.json 検証・flash-manifest 生成の unittest
├── scripts/
│   ├── build_release_bins.py      # bootloader/partitions/boot_app0/firmware を 0x0000 結合し manifest 生成
│   ├── pins_manifest.py           # pins.json の読み込み・検証・ヘッダ生成
│   ├── pio_pinout_header.py       # ビルド時に pinout_generated.h を生成する pre-script
│   └── pio_fw_version.py          # FW_VERSION 埋め込み
├── docs/ci/
│   └── release-firmware.yml       # .github/workflows/release-firmware.yml 用の CI/Release 定義
├── .github/workflows/
│   └── release-firmware.yml       # タグ v* / workflow_dispatch で自動ビルド＆Release 公開
├── README.md
└── SPEC.md
```

## 4. 初期ファームウェア機能（疎通・動作確認用）
1. **起動時出力**: 115200 bps でバージョン（ビルド時マクロまたは定数）・チップモデル・MAC/ChipID・リセット理由を出力。
2. **LED 制御（GPIO2）**: 起動時は 1 秒周期で点滅（`BLINK`）。
3. **定期ハートビート**: 5 秒ごとに `uptime_ms`, `free_heap`, `led_mode` を 1 行ログ出力。
4. **シリアルコマンド（改行 `\n` / `\r\n` 区切り）**:
   - `help`: コマンド一覧を表示
   - `ping`: `pong` を返す
   - `info`: チップ情報・ファームバージョン・空きヒープ・稼働時間を表示
   - `pinout`: `pins.json` の配線一覧（`GPIO 番号 方向 active=レベル ラベル` ＋ 注意行）を表示
   - `led on`: LED 常時点灯
   - `led off`: LED 消灯
   - `led blink`: LED 点滅（1秒周期）に戻す
   - 未知コマンド: エラーメッセージと `help` 案内を返す

## 5. Release 用バイナリ出力仕様
専用 Android 書き込みアプリ（`esp32-devkitc-flasher`）が迷わず書き込めるよう、以下の成果物を出力・添付する。

1. `esp32-devkitc-firmware-merged.bin`
   - `esptool.py --chip esp32 merge_bin` で以下の標準レイアウトを `0x0000` 基準で結合した単一ファイル（ファイルサイズ軽減のため余白埋めは標準仕様どおり）。
   - `0x1000`: `bootloader.bin`
   - `0x8000`: `partitions.bin`
   - `0xe000`: `boot_app0.bin`
   - `0x10000`: `firmware.bin`
2. `esp32-devkitc-firmware-app.bin`
   - アプリ領域のみのバイナリ（オフセット `0x10000`）。
3. `flash-manifest.json`
   - 形式例：
     ```json
     {
       "schemaVersion": 1,
       "chip": "esp32",
       "board": "esp32dev",
       "version": "0.1.1",
       "flashMode": "dio",
       "flashFreq": "40m",
       "flashSize": "4MB",
       "merged": {
         "filename": "esp32-devkitc-firmware-merged.bin",
         "offset": "0x0000",
         "sha256": "..."
       },
       "parts": [
         { "filename": "esp32-devkitc-firmware-app.bin", "offset": "0x10000", "sha256": "..." }
       ],
       "pins": [
         {
           "gpio": 2,
           "label": "オンボード青色LED / 外部LED出力",
           "direction": "output",
           "activeLevel": "HIGH",
           "note": "GPIO2 は内蔵LEDと共通。外部LEDを繋ぐ場合は 220〜330Ω 抵抗経由で GND へ接続"
         }
       ],
       "wiringNotes": [
         "3.3Vロジック専用（5V入力禁止）。5V を使う場合は VIN へ接続",
         "GPIO6〜11 は内蔵フラッシュ専用のため外部に接続しない"
       ],
       "powerNote": "3.3Vロジック専用（5V入力禁止）。5V を使う場合は VIN へ接続"
     }
     ```
   - `pins` / `wiringNotes` / `powerNote` は `pins.json` がある場合のみ付与する（無い場合はキー自体を出さない＝従来のスキーマと同一）。

## 6. ピン配線情報（`pins.json`）
書き込みアプリが「どの GPIO に何を繋ぐか」を自動表示できるよう、リポジトリ直下の `pins.json` を唯一の情報源とする。

1. **スキーマ（`schemaVersion: 1`）**
   - `board`: PlatformIO ボード ID（任意。`--board` と一致しない場合はエラー）
   - `pins[]`: 必須。`gpio`（整数 `0`〜`39`、`6`〜`11` はフラッシュ専用のため不可、重複不可）、`label`（非空文字列）、`direction`（`output` / `input` / `input_pullup` / `input_pulldown` / `input_output`）、`activeLevel`（`HIGH` / `LOW` / `NONE`、任意）、`note`（任意、非空文字列）
   - `powerNote`: 電源注意（任意、1 行）
   - `notes[]`: ボード共通の注意点（任意、各要素は非空文字列）
   - 未知のキーは誤字検出のためエラー。文字列は前後空白を除いて 512 文字まで。
2. **ビルドへの反映**
   - `scripts/pio_pinout_header.py`（`pins.json` を `pinout_generated.h` に変換する pre-script）を `esp32dev` / `native` 両環境に適用。`pinout.cpp` は生成ヘッダを参照するので、ファームウェアの `pinout` 出力と `flash-manifest.json` の `pins` は常に一致する。
   - ボード照合: 実機環境では `BOARD`（`esp32dev`）と `pins.json` の `board` を照合し、一致しなければビルドを中止する。ボードを持たない `native` 環境では照合しない。
   - `PINS_JSON` 環境変数で別ファイルを指定可能。`pins.json` が無い場合もビルドは成功し、空テーブルとして扱う。
   - **別ファイルを使う場合は、ビルドの `PINS_JSON` とリリース生成の `--pins` に同じファイルを指定すること。** リリース生成側は `--pins` が無いとルートの `pins.json` を読むため、片方だけ差し替えるとファームウェアの `pinout` と `flash-manifest.json` の `pins` がずれる。
3. **検証**
   - `scripts/build_release_bins.py` は読み込み時に（`--pins` またはリポジトリの `pins.json`）検証し、生成後の `flash-manifest.json` も `--verify-only` で再検証する。
   - `tests/python/` の unittest がスキーマ検証・マニフェスト埋め込み・後方互換を確認する。

## 7. CI / Release 自動化要件
- **PR / push 検証**:
  - `pio test -e native`（ホスト環境でのユニットテスト）
  - `pio run -e esp32dev`（ESP32-DevKitC 向けビルド検証）
  - `scripts/build_release_bins.py` による結合 `.bin` と `flash-manifest.json` 生成・検証- **Release 自動作成**:
  - タグ `v*`（例: `v0.1.0`）が push されたとき、または `workflow_dispatch` でリリースバージョンを指定したときに、上記アセット 3 点を添付して GitHub Release を公開する。
- 注意: GitHub App 権限等で `.github/workflows/` の push が制限される場合は `docs/ci/` に配置し、作成後に `.github/workflows/` へ反映する。

## 8. 初回リリース目標
- `v0.1.1` として、`flash-manifest.json` に `pins` / `wiringNotes` を含む 3 点のアセットを添付した GitHub Release を公開すること。
