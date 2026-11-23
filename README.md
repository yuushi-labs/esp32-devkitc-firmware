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
