# esp32-devkitc-firmware

ESP32-DevKitC 向けのファームウェアリポジトリです。GitHub Actions / Release でビルド済み `.bin`（単一結合バイナリ＋アプリ単体バイナリ）を構築し、専用 Android アプリ [`yuushi-labs/esp32-devkitc-flasher`](https://github.com/yuushi-labs/esp32-devkitc-flasher) から USB OTG 経由でそのまま書き込める構成を目指しています。

## ターゲット環境
- **ボード**: ESP32-DevKitC（`esp32dev` / 無印 ESP32）
- **ビルドシステム**: PlatformIO（`espressif32`）
- **シリアル通信**: 115200 bps

## 初期ファームウェア（動作確認サンプル）
- オンボード LED（GPIO2）の点滅制御
- シリアルモニター（115200 bps）での起動バナー・チップ情報・稼働時間・空きヒープ出力
- 簡易シリアルコマンド対応（`help`, `ping`, `info`, `led on`, `led off`, `led blink`）

## Release アセット
Release 公開時に以下のファイルを構築・添付します：
- `esp32-devkitc-firmware-merged.bin` — `0x0000` から一発で書き込める結合済み単一バイナリ（bootloader + partitions + boot_app0 + firmware）
- `esp32-devkitc-firmware-app.bin` — `0x10000` 向けのアプリ単体バイナリ
- `flash-manifest.json` — 各 `.bin` のオフセット・SHA-256・バージョン情報

詳細は [SPEC.md](./SPEC.md) を参照してください。

## 開発

### 必要環境
- PlatformIO Core（`pip install platformio`）
- Python 3.9 以上（`scripts/build_release_bins.py` 用）

### ビルドとテスト
```bash
pio test -e native             # ホスト環境でのユニットテスト
pio run -e esp32dev            # ESP32-DevKitC 向けビルド
pio run -e esp32dev -t upload  # 書き込み（USB 接続時）
pio device monitor             # 115200 bps でシリアルモニター
```

### リリース用バイナリの生成
```bash
python scripts/build_release_bins.py \
  --build-dir .pio/build/esp32dev \
  --output-dir dist \
  --version 0.1.0
```
`dist/` に `esp32-devkitc-firmware-merged.bin` / `esp32-devkitc-firmware-app.bin` / `flash-manifest.json` が生成され、オフセットと SHA-256 が自動検証されます。`--verify-only` を付けると既存の `dist/` を再検証のみ行います。

### バージョンの埋め込み
`FW_VERSION=0.2.0 pio run -e esp32dev` のように環境変数で指定します。未指定時は `src/app_config.h` の既定値（`0.1.0`）が使われます。

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
