# esp32-devkitc-firmware

ESP32-DevKitC 向けのファームウェアリポジトリです。GitHub Actions / Release でビルド済み `.bin`（単一結合バイナリ＋アプリ単体バイナリ）を構築し、専用 Android アプリ [`yuushi-labs/esp32-devkitc-flasher`](https://github.com/yuushi-labs/esp32-devkitc-flasher) から USB OTG 経由でそのまま書き込める構成を目指しています。

## ターゲット環境
- **ボード**: ESP32-DevKitC（`esp32dev` / 無印 ESP32）
- **ビルドシステム**: PlatformIO（`espressif32`）
- **シリアル通信**: 115200 bps

## 初期ファームウェア（動作確認サンプル）
- オンボード LED（GPIO2）の点滅制御
- シリアルモニター（115200 bps）での起動バナー・チップ情報・稼働時間・空きヒープ出力
- 簡易シリアルコマンド対応（`help`, `ping`, `info`, `pinout`, `led on`, `led off`, `led blink`）
- `pins.json` に書いた GPIO 配線（役割・方向・注意点）を `pinout` コマンドで表示

## Release アセット
Release 公開時に以下のファイルを構築・添付します：
- `esp32-devkitc-firmware-merged.bin` — `0x0000` から一発で書き込める結合済み単一バイナリ（bootloader + partitions + boot_app0 + firmware）
- `esp32-devkitc-firmware-app.bin` — `0x10000` 向けのアプリ単体バイナリ
- `flash-manifest.json` — 各 `.bin` のオフセット・SHA-256・バージョン情報、およびピン配線（`pins` / `wiringNotes`）

## ピン配線情報（`pins.json`）
ルートの `pins.json` が「どの GPIO に何を繋ぐか」の唯一の定義です。次の 2 か所に同じ内容が行き渡ります。

- **書き込みアプリ**: `scripts/build_release_bins.py` が `flash-manifest.json` の `pins` / `wiringNotes` に埋め込む → [`esp32-devkitc-flasher`](https://github.com/yuushi-labs/esp32-devkitc-flasher) が配線ガイドとして表示
- **ファームウェア**: `scripts/pio_pinout_header.py` がビルド時に `pinout_generated.h` を生成 → `pinout` コマンドが同じ一覧をシリアル出力

```json
{
  "schemaVersion": 1,
  "board": "esp32dev",
  "pins": [
    {
      "gpio": 2,
      "label": "オンボード青色LED / 外部LED出力",
      "direction": "output",
      "activeLevel": "HIGH",
      "note": "GPIO2 は内蔵LEDと共通。外部LEDを繋ぐ場合は 220〜330Ω 抵抗経由で GND へ接続"
    }
  ],
  "powerNote": "3.3Vロジック専用（5V入力禁止）。5V を使う場合は VIN へ接続",
  "notes": ["GPIO6〜11 は内蔵フラッシュ専用のため外部に接続しない"]
}
```

| キー | 必須 | 内容 |
| --- | --- | --- |
| `schemaVersion` | ✔ | 現在は `1` のみ |
| `board` | | PlatformIO ボード ID。`--board`（リリース生成）/ ビルドの `BOARD` と違うとビルドが止まります |
| `pins` | ✔ | 1 件以上の配列（`gpio` の重複不可） |
| `pins[].gpio` | ✔ | 整数 `0`〜`39`。`6`〜`11` はフラッシュ専用のため指定不可 |
| `pins[].label` | ✔ | 空でない文字列（例: `"BOOTボタン (書き込みモード)"`） |
| `pins[].direction` | ✔ | `output` / `input` / `input_pullup` / `input_pulldown` / `input_output` |
| `pins[].activeLevel` | | `HIGH` / `LOW` / `NONE`（大文字小文字は不問、保存時に大文字化） |
| `pins[].note` | | 空でない文字列（配線の注意点） |
| `powerNote` | | 電源まわりの注意（1 行） |
| `notes` | | ボード共通の注意点の配列（`GPIO6〜11` など） |

文字列は前後の空白を除いて 512 文字までです。`pins.json` が無いリポジトリでもビルドは通り、`flash-manifest.json` に `pins` を付けず、`pinout` は「ピン情報なし」と表示します（後方互換）。

別のファイルを使うときは、ビルドとリリース生成の両方に同じファイルを渡してください（`--pins path/to/pins.json` と `PINS_JSON=path/to/pins.json pio run -e esp32dev`）。片方だけ差し替えるとファームウェアの `pinout` とマニフェストの `pins` がずれます。

詳細は [SPEC.md](./SPEC.md) を参照してください。

## 開発

### 必要環境
- PlatformIO Core（`pip install platformio`）
- Python 3.9 以上（`scripts/build_release_bins.py` / `tests/python` 用）

### ビルドとテスト
```bash
pio test -e native             # ホスト環境でのユニットテスト
pio run -e esp32dev            # ESP32-DevKitC 向けビルド
pio run -e esp32dev -t upload  # 書き込み（USB 接続時）
pio device monitor             # 115200 bps でシリアルモニター
python3 -m unittest discover -s tests/python   # pins.json / マニフェスト生成のテスト
```

### リリース用バイナリの生成
```bash
python scripts/build_release_bins.py \
  --build-dir .pio/build/esp32dev \
  --output-dir dist \
  --version 0.1.0
```
`dist/` に `esp32-devkitc-firmware-merged.bin` / `esp32-devkitc-firmware-app.bin` / `flash-manifest.json` が生成され、オフセットと SHA-256 が自動検証されます。`pins.json` があれば `pins` / `wiringNotes` も検証付きで埋め込まれます。`--verify-only` を付けると既存の `dist/` を再検証のみ行います。

### バージョンの埋め込み
`FW_VERSION=0.2.0 pio run -e esp32dev` のように環境変数で指定します。未指定時は `src/app_config.h` の既定値（`0.1.1`）が使われます。

### CI / Release
`.github/workflows/release-firmware.yml`（`docs/ci/release-firmware.yml` と同一）が以下を実行します。

- PR / `main` push: `pio test -e native` → `pio run -e esp32dev` → 結合バイナリ生成と検証 → アーティファクト保存
- タグ `v*` push、または `workflow_dispatch`（`publish: true`）: 上記 3 アセットを添付した GitHub Release を作成（既存 Release にはアップロード）

### シリアルコマンド
| コマンド | 動作 |
| --- | --- |
| `help` | コマンド一覧を表示 |
| `ping` | `pong` を返す |
| `info` | チップ情報・ファームバージョン・空きヒープ・稼働時間を表示 |
| `pinout` | `pins.json` のピン配線一覧（GPIO 番号・役割・注意点）を表示 |
| `led on` | LED 常時点灯 |
| `led off` | LED 消灯 |
| `led blink` | LED 点滅（1 秒周期） |

5 秒ごとに `heartbeat uptime_ms=... free_heap=... led_mode=...` を出力します。

### 結合バイナリのレイアウト
| オフセット | 内容 |
| --- | --- |
| `0x1000` | `bootloader.bin` |
| `0x8000` | `partitions.bin` |
| `0xe000` | `boot_app0.bin` |
| `0x10000` | `firmware.bin`（= `esp32-devkitc-firmware-app.bin`） |

`esp32-devkitc-firmware-merged.bin` は `0x0000` から書き込めるよう `0xFF` でパディングした結合イメージで、`esptool.py merge_bin` の出力とバイト単位で一致します。
