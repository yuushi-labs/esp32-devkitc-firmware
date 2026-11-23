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
├── src/
│   ├── main.cpp
│   └── command_handler.h / .cpp   # シリアルコマンド解析（単体テスト可能に分離）
├── test/                          # PlatformIO native テスト（コマンド解析・フォーマット等）
├── scripts/
│   └── build_release_bins.py      # bootloader/partitions/boot_app0/firmware を 0x0000 結合し manifest 生成
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
       "version": "0.1.0",
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
       ]
     }
     ```

## 6. CI / Release 自動化要件
- **PR / push 検証**:
  - `pio test -e native`（ホスト環境でのユニットテスト）
  - `pio run -e esp32dev`（ESP32-DevKitC 向けビルド検証）
  - `scripts/build_release_bins.py` による結合 `.bin` と `flash-manifest.json` 生成・検証
- **Release 自動作成**:
  - タグ `v*`（例: `v0.1.0`）が push されたとき、または `workflow_dispatch` でリリースバージョンを指定したときに、上記アセット 3 点を添付して GitHub Release を公開する。
- 注意: GitHub App 権限等で `.github/workflows/` の push が制限される場合は `docs/ci/` に配置し、作成後に `.github/workflows/` へ反映する。

## 7. 初回リリース目標
- `v0.1.0` として、上記 3 点のアセットを添付した GitHub Release を公開すること。
