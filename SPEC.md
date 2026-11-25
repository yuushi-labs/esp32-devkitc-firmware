# esp32-devkitc-firmware 設計仕様書 (SPEC) — v0.3.0

## 1. 目的
ESP32-DevKitC 用のファームウェアを GitHub 上で管理・自動ビルドし、GitHub Releases に `.bin` を公開することで、PC を使わずに専用 Android アプリ（`yuushi-labs/esp32-devkitc-flasher`）から直接書き込めるようにする。

v0.3.0 では、既存の 3 パーツ（SSD1306 OLED + エンコーダ + 2 ボタン / BMP280 / DHT）の配線を一切変えずに、**Wi-Fi と Bluetooth（BLE）をフル活用した多機能ガジェットステーション**へ拡張する。

1 リポジトリ・1 コードベースから **2 つのビルドターゲット** を提供する。

| ターゲット | 環境 | 目的 |
| --- | --- | --- |
| `station`（🎛️ 多機能環境ステーション） | `esp32dev_station` | 環境モニター・グラフ・NTP 付き LittleFS 記録・Wi-Fi Web ダッシュボード＆QR・BLE センサー＆メディアリモコン・物理タイマー |
| `tester`（🧪 パーツ動作確認） | `esp32dev_tester` | I2C 全スキャン、BMP280 / DHT の応答、エンコーダ・ボタン導通を表示（v0.2.0 相当を維持） |

## 2. ハードウェア仕様
- ボード: **ESP32-DevKitC**（PlatformIO ボード ID: `esp32dev`）
- MCU: ESP32（Xtensa Dual-Core 32-bit LX6）、フラッシュ 4MB、`dio` / `40m`
- オンボード LED: GPIO2
- UART: 115200 bps
- パーティション: Wi-Fi と BLE を同時に搭載するため、アプリ領域が十分確保できる **`no_ota.csv`**（app 領域 2MB + SPIFFS/LittleFS 領域 約 1.9MB）等を基準とし、実ビルドサイズに応じて選定する。`scripts/build_release_bins.py` は選択されたパーティション CSV から `filesystems` のオフセット・サイズを自動抽出してマニフェストへ反映する。

### 2.1 ピン割り当て（v0.2.0 から変更なし）
`pins.json` が唯一の情報源。ファームウェアとマニフェストは同じファイルから生成される。

| GPIO | 役割 | 方向 | 備考 |
| --- | --- | --- | --- |
| 21 | I2C SDA（OLED + BMP280） | input_output | 並列接続 |
| 22 | I2C SCL（OLED + BMP280） | input_output | 並列接続 |
| 32 | エンコーダ A 相 (TRA) | input_pullup | 入力専用ピン（内部プルアップあり） |
| 33 | エンコーダ B 相 (TRB) | input_pullup | 同上 |
| 25 | エンコーダ押し込み (PSH) | input_pullup / LOW | 決定・間隔切替・再生/停止など |
| 26 | 戻る (BAK) | input_pullup / LOW | 戻る・前の曲（長押しでメニューへ） |
| 27 | 決定 (CON) | input_pullup / LOW | 決定・手動記録・次の曲など |
| 4 | DHT DATA | input_output | 10kΩ で 3.3V へプルアップ |
| 2 | LED | output / HIGH | 状態通知・アラート点滅 |
| 0 | BOOT | input_pullup / LOW | 書き込みモード |

- 3.3V ロジック専用（5V 入力禁止）。OLED・BMP280・DHT の VCC は 3.3V で共通。
- BMP280 の `CSB` は 3.3V（I2C 固定）、`SDO` は GND（`0x76`）または 3.3V（`0x77`）に対応。

### 2.2 I2C デバイス
| デバイス | アドレス | 判定方法 |
| --- | --- | --- |
| SSD1306 OLED 128x64 | `0x3C` → `0x3D` の順に探索 | ACK 応答 |
| BMP280 / BME280 | `0x76` → `0x77` の順に探索 | レジスタ `0xD0` のチップ ID（`0x58` / `0x60`） |

I2C は 100 kHz。`Wire.setTimeOut(50)` で未接続デバイスによるブロックを避ける。

## 3. 共通機能
1. 起動バナー: バージョン・ボード・チップモデル・チップ ID・リセット理由・`target=station|tester`
2. LED（GPIO2）: 起動時 1 秒周期点滅、`led on/off/blink` で切替（アラート・タイマー終了時は高速点滅）
3. ハートビート: 5 秒ごとに `uptime_ms`, `free_heap`, `led_mode`
4. シリアルコマンド: `help`, `ping`, `info`, `pinout`, `led on|off|blink` に加え、ターゲット別コマンド（§4.7 / §5）
5. コマンドは `\n` / `\r\n` 区切り、前後空白と大文字小文字を無視

## 4. 🎛️ station（多機能環境ステーション）

### 4.1 全体メニューと操作体系
ホームメニューをロータリーエンコーダ（`TRA/TRB`）で選択し、`CON` または `PSH` で各機能に入る。どのモードからでも `BAK` 長押し（またはメニュー項目）でホームへ戻れる。

| # | モード | 概要 |
| --- | --- | --- |
| 1 | 📊 環境モニター＆グラフ | 現在の温湿度・気圧・不快指数・天気傾向、OLED ミニ折れ線グラフ、しきい値アラート |
| 2 | 📁 ログ管理（LittleFS） | NTP 時刻付き CSV 記録の ON/OFF、記録間隔切替、件数・容量表示 |
| 3 | 🌐 Wi-Fi ＆ Web ダッシュボード | AP 設定モード、NTP 時刻同期、スマホ用 Web UI、JSON API、OLED 上の QR コード表示 |
| 4 | 📶 Bluetooth（BLE） | センサー値配信（GATT）＋ **BLE メディアリモコンモード**（音量・再生/停止・曲送り） |
| 5 | ⏱️ 物理タイマー | エンコーダで 1〜60 分設定・ポモドーロ（25分/5分）対応カウントダウン |
| 6 | 🛠️ システム・診断 | IP / RSSI、BLE 状態、I2C スキャン、LittleFS 容量、Wi-Fi 設定初期化 |

### 4.2 📊 環境モニター＆ミニグラフ
- **計測項目**: 温度（BMP280 優先、未接続時は DHT）、湿度（DHT）、気圧（BMP280）、不快指数（快適 / やや暑い / 乾燥など）、気圧トレンド（過去推移から ☀️/☁️/☔ 傾向を算出）
- **OLED ミニグラフ**: 直近のメモリ内リングバッファ（例: 最大 64〜96 サンプル）から温度・気圧の折れ線グラフを 128x64 OLED に描画。エンコーダで「サマリー ↔ グラフ ↔ 詳細」を切替
- **アラート**: 高温（例: ≥ 30℃）・乾燥（例: ≤ 35%）・気圧急変時に画面アイコン点滅＋LED 通知（設定で ON/OFF 可）

### 4.3 📁 LittleFS ログ（NTP 時刻対応）
- マウント: `LittleFS.begin(true)`（未フォーマット時は自動フォーマット）
- パス: `/log.csv`
- 形式: ヘッダー `timestamp,uptime_ms,temp_c,pressure_hpa,humidity_pct`
  - Wi-Fi 接続＋NTP 同期済み: `timestamp` に `YYYY-MM-DD HH:MM:SS`（JST `UTC+9`）を記録
  - 未同期時: `timestamp` は空欄または `uptime_ms` のみ記録し、列数は固定
- 記録間隔: `10s / 60s / 300s` を `PSH` で切替、`CON` 短押しで手動記録、長押しで記録 ON/OFF
- マニフェストの `filesystems` にパーティション情報を記載し、`esp32-devkitc-flasher` の吸い出し機能と互換を保つ

### 4.4 🌐 Wi-Fi ＆ Web ダッシュボード
- **初回 Wi-Fi 設定（AP モード / Captive Portal）**:
  - NVS（`Preferences`）に SSID が未保存の場合、またはユーザーが「Wi-Fi 設定モード」を選んだ場合、AP（`ESP32-Station-AP`）を起動
  - スマホで接続して `http://192.168.4.1/`（またはリダイレクト）を開くと、周辺 SSID スキャン一覧から選んでパスワードを入力・保存し、通常モードへ再接続できる
- **Web ダッシュボード（`http://<ESP32のIP>/`）**:
  - 外部 CDN に依存しない軽量 HTML/CSS/SVG で完結（オフライン LAN でも動作）
  - リアルタイム温湿度・気圧・不快指数表示（自動ポーリング）
  - 履歴グラフ表示、`/log.csv` ダウンロードボタン、ログ消去ボタン
- **JSON API**:
  - `GET /api/status`: 現在のセンサー値・Wi-Fi/BLE/ログ状態
  - `GET /api/logs`: 直近の履歴データ（JSON 配列）
  - Raspberry Pi や他アプリからも連携しやすい構成
- **OLED QR コード表示**:
  - Wi-Fi 接続中に Wi-Fi 画面で `CON` を押すと `http://<IP>/` の QR コードを 128x64 OLED に描画し、スマホのカメラで読み取るだけで即開ける

### 4.5 📶 Bluetooth（BLE）2 機能
- Flash / RAM 節約のため **NimBLE**（または軽量 BLE 構成）を採用する
- **① センサー配信（BLE GATT サーバー）**:
  - デバイス名: `ESP32-EnvStation`
  - Environmental Sensing Service（`0x181A`）相当の温度・湿度・気圧特性＋状態通知特性を公開し、Wi-Fi が無い環境でもスマホから読み取れる
- **② BLE メディアリモコン（HID コンシューマーコントロール）**:
  - リモコンモード選択中のみ、エンコーダとボタンをスマホ/PC 向けの操作入力として送信：
    - エンコーダ右回転 → **音量 UP** / 左回転 → **音量 DOWN**
    - `PSH`（押し込み）→ **再生 / 一時停止**
    - `CON`（短押し）→ **次の曲**
    - `BAK`（短押し）→ **前の曲**（長押しでメニューへ戻る）
  - モードを抜けたら通常のローカル操作に戻る

### 4.6 ⏱️ 物理タイマー / ポモドーロ
- エンコーダ回転で **1〜60 分** を 1 分刻みで調整、`PSH` でポモドーロ（25 分作業 / 5 分休憩）プリセット切替
- `CON` で開始 / 一時停止、`BAK` 短押しでリセット（長押しでホームへ戻る）
- 残り時間を 2 倍角フォント＋プログレスバーで OLED 表示し、00:00 到達で OLED 反転点滅＋LED 通知

### 4.7 シリアルコマンド（station）
- 既存: `scan`, `sensors`, `log`, `log on`, `log off`, `log now`, `log clear`, `page`, `page 1..N`
- 追加: `wifi status`, `wifi ap`, `wifi clear`, `ble status`, `ble hid on|off`, `timer <min>`, `timer start|pause|reset`, `mode <name>`

## 5. 🧪 tester（パーツ動作確認）
v0.2.0 の仕様をそのまま維持する（I2C 全スキャン・BMP280/DHT テスト・エンコーダ/ボタン導通テスト・シリアル `scan/sensors/inputs`）。

## 6. センサー・通信異常時の挙動
- BMP280 / DHT / OLED / LittleFS 未接続・異常時は v0.2.0 と同じく他機能を停止させずフォールバック継続する
- Wi-Fi 未設定・切断時も、環境モニター・LittleFS 記録・BLE・物理タイマーはオフラインでそのまま動作し、バックグラウンドで再接続または AP 待機に留める（ブロッキングしない）

## 7. テスト・品質要件
- `pio test -e native`: Arduino 非依存のロジック（センサー補正、グラフバッファ、QR 生成、CSV/NTP フォーマット、タイマー状態機械、入力遷移、コマンド解析など）をホスト上で網羅的にユニットテストする
- `python3 -m unittest discover -s tests/python`: `pins.json` とマニフェスト生成を検証する
- `pio run -e esp32dev_station` および `pio run -e esp32dev_tester`: 警告なしビルドおよびパーティション容量内に収まることを確認する

## 8. Release 用バイナリ出力仕様
v0.2.0 と同じく `scripts/build_release_bins.py --variant <station|tester>` で 6 アセット（`station` の `flash-manifest.json` が既定、`tester` は `tester-flash-manifest.json`）を生成・検証し、GitHub Release へ公開する。
