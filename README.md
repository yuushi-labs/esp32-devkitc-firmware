# esp32-devkitc-firmware

ESP32-DevKitC 向けのファームウェアリポジトリです。1 つのコードベースから **2 種類のファームウェア** をビルドし、GitHub Actions / Release で書き込み用 `.bin` を配布します。専用 Android アプリ [`yuushi-labs/esp32-devkitc-flasher`](https://github.com/yuushi-labs/esp32-devkitc-flasher) から USB OTG 経由でそのまま書き込めます。

| ターゲット | PlatformIO 環境 | 用途 |
| --- | --- | --- |
| 🎛️ `station`（多機能環境ステーション） | `esp32dev_station` | 温度・気圧・湿度・不快指数の表示、OLED ミニグラフ、NTP 時刻付き LittleFS 記録、Wi-Fi Web ダッシュボード＆QR、BLE センサー配信＆メディアリモコン、物理タイマー／ポモドーロ |
| 🧪 `tester`（パーツ動作確認） | `esp32dev_tester` | I2C バス全スキャン、BMP280 / DHT の応答確認、エンコーダ・3 ボタンの導通を OLED とシリアルにリアルタイム表示（v0.2.0 相当） |

v0.3.0 でも **配線は v0.2.0 から変更ありません**（`pins.json` もそのまま）。`tester` で配線とパーツを確認してから `station` を書き込む、という使い方を想定しています。

## ターゲット環境
- **ボード**: ESP32-DevKitC（`esp32dev` / 無印 ESP32、4MB フラッシュ）
- **ビルドシステム**: PlatformIO（`espressif32@6.9.0`、Arduino framework）
- **シリアル通信**: 115200 bps
- **外部ライブラリ**: [`h2zero/NimBLE-Arduino`](https://github.com/h2zero/NimBLE-Arduino)（BLE 用）のみ。OLED・BMP280・DHT・QR コードは本リポジトリ内で実装
- **パーティション**: `no_ota.csv`（app 2MB + LittleFS 約 1.9MB）。Wi-Fi + Web サーバー + BLE を同時に積むと `default.csv` の 1.25MB アプリ枠に収まらないため、OTA を外してアプリ領域を広げています。書き込みは USB 経由の Android アプリで行います
- **バージョン**: `src/app_config.h` の既定値は `0.3.0`。リリースビルドでは `FW_VERSION` で上書きします

## ハードウェア構成

| パーツ | 接続 | 備考 |
| --- | --- | --- |
| OLED + エンコーダ + ボタン一体モジュール | `SDA`→GPIO21, `SCL`→GPIO22, `VCC`→3.3V, `GND`→GND | SSD1306 128x64（`0x3C` / `0x3D` 自動探索）。`BAK`→GPIO26, `CON`→GPIO27, `PSH`→GPIO25, `TRA`→GPIO32, `TRB`→GPIO33 |
| 大気圧センサー BMP280 | `SDA`/`SCL` は OLED と並列、`VCC`→3.3V | `0x76` / `0x77` 自動探索、チップ ID で BMP280 / BME280 を判定 |
| 温湿度センサー DHT11 / DHT22 | `DATA`→GPIO4（10kΩ で 3.3V へプルアップ） | 未接続でも停止せず湿度だけスキップ。DHT11 / DHT22 は自動判定 |

すべて 3.3V ロジックです（5V を直接入れない）。GPIO34〜39 は入力専用で内部プルアップがないため、エンコーダとボタンには GPIO25/26/27/32/33 を使っています。

配線の唯一の情報源はリポジトリ直下の [`pins.json`](./pins.json) です。次の 2 か所へ同じ内容が行き渡ります。

- **書き込みアプリ**: `scripts/build_release_bins.py` が `flash-manifest.json` の `pins` / `wiringNotes` に埋め込む → アプリのボード図が該当ピンをハイライト表示
- **ファームウェア**: `scripts/pio_pinout_header.py` がビルド時に `pinout_generated.h` を生成 → `pinout` コマンドが同じ一覧をシリアル出力

```json
{
  "schemaVersion": 1,
  "board": "esp32dev",
  "pins": [
    {
      "gpio": 21,
      "label": "I2C SDA (OLED SDA + BMP280 SDA)",
      "direction": "input_output",
      "note": "OLED モジュールと BMP280 の SDA を並列に接続"
    }
  ],
  "powerNote": "3.3Vロジック専用（5V入力禁止）…",
  "notes": ["GPIO6〜11 は内蔵フラッシュ専用のため外部に接続しない"]
}
```

| キー | 必須 | 内容 |
| --- | --- | --- |
| `schemaVersion` | ✔ | 現在は `1` のみ |
| `board` | | PlatformIO ボード ID。`--board`（リリース生成）/ ビルドの `BOARD` と違うとビルドが止まります |
| `pins` | ✔ | 1 件以上の配列（`gpio` の重複不可） |
| `pins[].gpio` | ✔ | 整数 `0`〜`39`。`6`〜`11` はフラッシュ専用のため指定不可 |
| `pins[].label` | ✔ | 空でない文字列 |
| `pins[].direction` | ✔ | `output` / `input` / `input_pullup` / `input_pulldown` / `input_output` |
| `pins[].activeLevel` | | `HIGH` / `LOW` / `NONE` |
| `pins[].note` | | 配線の注意点（1 行） |
| `powerNote` | | 電源まわりの注意（1 行） |
| `notes` | | ボード共通の注意点の配列 |

文字列は前後の空白を除いて 512 文字までです。`pins.json` が無いリポジトリでもビルドは通り、`flash-manifest.json` に `pins` を付けず、`pinout` は「ピン情報なし」と表示します（後方互換）。

別のファイルを使うときは、ビルドとリリース生成の両方に同じファイルを渡してください（`--pins path/to/pins.json` と `PINS_JSON=path/to/pins.json pio run -e esp32dev_station`）。片方だけ差し替えるとファームウェアの `pinout` とマニフェストの `pins` がずれます。

## 共通のシリアル機能

- 起動バナー（バージョン・ボード・ターゲット・チップ情報・リセット理由）
- オンボード LED（GPIO2）の 1 秒点滅
- 5 秒ごとの `heartbeat uptime_ms=... free_heap=... led_mode=...`
- 共通コマンド: `help` / `ping` / `info` / `pinout` / `led on|off|blink`

## 🎛️ station（多機能環境ステーション）

ホームメニューをエンコーダで選び、`CON` または `PSH` で入ります。どのモードからでも `BAK` 長押しでホームへ戻れます。

| # | モード | 内容 |
| --- | --- | --- |
| 1 | 📊 環境モニター | 温度（BMP280 優先、未接続時は DHT）・気圧・湿度・不快指数・天気傾向・記録状態。`CON` でサマリー → グラフ → 詳細を切替 |
| 2 | 📁 ログ管理 | NTP 時刻付き CSV の ON/OFF、記録間隔、件数、次回まで、ファイルサイズ |
| 3 | 🌐 Wi-Fi ＆ Web | 接続状態・IP・RSSI・NTP 時刻。未設定時は AP 設定モード。`CON` で QR コード表示 |
| 4 | 📶 Bluetooth | GATT センサー配信とメディアリモコン（HID）モードの切替・接続状態 |
| 5 | ⏱️ タイマー | 1〜60 分カウントダウン／ポモドーロ（25 分作業・5 分休憩） |
| 6 | 🛠️ システム | バージョン・稼働時間・空きヒープ・I2C アドレス・OLED・Wi-Fi・BLE・ログ状態 |

### 環境モニター
- 温度が 2 倍角フォントで大きく表示され、気圧・湿度・不快指数・天気傾向（sunny / cloudy / rain）を 1 画面にまとめます
- グラフ画面は直近 64 サンプル（10 秒間隔）の温度・気圧を 2 本のミニ折れ線で描画します
- 詳細画面は BMP280 のチップ名・アドレス、気圧トレンド、DHT の値、不快指数を表示します
- しきい値アラート: 高温 **30℃ 以上**、乾燥 **35% 以下**、気圧急変 **1.5 hPa 以上**。画面に `! HOT31.0 DRY30 PJMP` のように表示し、オンボード LED を高速点滅します

### ボタン操作（モード別）
| 操作 | 環境モニター | ログ | Wi-Fi | BLE（HID モード） | タイマー |
| --- | --- | --- | --- | --- | --- |
| エンコーダ回転 | サマリー↔グラフ↔詳細 | — | — | 音量 UP / DOWN | 1〜60 分を調整（停止中のみ） |
| `PSH` | 次の表示 | 記録間隔を切替（10 秒→1 分→5 分） | AP 設定モードを開始 | 再生 / 一時停止 | ポモドーロ切替 |
| `CON` 短押し | 次の表示 | 手動で 1 件記録 | QR コードを表示 | 次の曲 | 開始 / 一時停止 |
| `BAK` 短押し | 前の表示 | 前の表示 | 前の表示 | 前の曲 | リセット |
| `CON` 長押し（0.9 秒） | — | 記録 ON / OFF | — | — | — |
| `BAK` 長押し（0.9 秒） | ホームへ | ホームへ | ホームへ | ホームへ | ホームへ |

タイマーは 00:00 で OLED を反転点滅し、LED で通知します。

### LittleFS ログ
- `LittleFS.begin(true)` でマウント（未フォーマットなら自動フォーマット）
- `/log.csv` のヘッダーは `timestamp,uptime_ms,temp_c,pressure_hpa,humidity_pct`
- Wi-Fi + NTP 同期時は `timestamp` に `YYYY-MM-DD HH:MM:SS`（JST `UTC+9`）を記録し、未同期時は空欄
- 未接続センサーの列は空欄（例: `,1234,23.45,,`）になり、列数は常に一定
- `no_ota.csv` の `spiffs` 領域（`0x210000` / 1,966,080 B）を使います。`esp32-devkitc-flasher` の LittleFS 吸い出しでそのまま `/log.csv` を読み出せます

### Wi-Fi ＆ Web ダッシュボード
- **初回設定（AP モード）**: SSID `ESP32-Station-AP`（オープン）に接続し、`http://192.168.4.1/` のフォームで周辺 SSID を選びパスワードを保存します。認証情報は NVS（`Preferences`）に保存され、次回起動時に自動接続します
- **ダッシュボード**: `http://<ESP32 の IP>/` に外部 CDN 非依存の HTML/CSS/JS を配信します。温湿度・気圧・不快指数・Wi-Fi/BLE/ログ/タイマー状態を自動更新し、履歴グラフ・`/log.csv` ダウンロード・ログ消去を提供します
- **JSON API**: `GET /api/status`（現在値と各機能の状態）、`GET /api/logs`（直近 240 件の履歴）、`GET /download`（CSV）
- **QR コード**: Wi-Fi モードで `CON` を押すか、Web ダッシュボードの「Show QR on OLED」を押すと `http://<IP>/` の QR コードを OLED に描画します（内蔵エンコーダ、ISO/IEC 18004 byte mode・version 1〜10）
- 未設定・切断時も環境モニター・ログ・BLE・タイマーはオフラインで動作し、Wi-Fi はブロッキングせずバックグラウンドで再接続します

### Bluetooth（BLE）
- **センサー配信**: デバイス名 `ESP32-EnvStation`。Environmental Sensing Service（`0x181A`）相当の温度・湿度・気圧特性と状態通知（2 秒間隔）を公開します
- **メディアリモコン**: `ble hid on` または BLE 画面の `PSH` で有効化します。エンコーダ右/左 → 音量 UP/DOWN、`PSH` → 再生/一時停止、`CON` → 次の曲、`BAK` → 前の曲。HID レポートは 30ms で自動リリースします
- BLE スタックは RAM/Flash の小さい NimBLE を使います

### シリアルコマンド（station）
| コマンド | 動作 |
| --- | --- |
| `scan` | I2C バスを再スキャンして一覧表示 |
| `sensors` | BMP280 / DHT / 不快指数 / 天気傾向 / アラートを表示 |
| `log` / `log on` / `log off` / `log now` / `log clear` | ログ状態の表示・開始・停止・手動記録・消去 |
| `page` / `page 1..6` | 次のモード / 指定モードへ |
| `mode` / `mode NAME` | 現在の画面・モード表示 / `env` `logs` `wifi` `ble` `timer` `system` `menu` を開く |
| `wifi` / `wifi ap` / `wifi clear` | Wi-Fi・NTP 状態 / AP 設定モード開始 / 保存済み認証情報を消去 |
| `ble` / `ble hid on` / `ble hid off` | BLE 状態 / メディアリモコンモード ON・OFF |
| `timer` / `timer N` / `timer start|pause|reset` | タイマー状態 / 1〜60 分設定 / 開始・一時停止・リセット |

## 🧪 tester（パーツ動作確認）

起動時に I2C バス（`0x01`〜`0x7F`）を全スキャンし、見つかったアドレスとデバイス名（`0x3C` SSD1306 OLED、`0x76`/`0x77` BMP280 / BME280 など）を OLED とシリアルへ表示します。5 秒ごとに再スキャンするので、配線を触りながら確認できます。

- 画面 1（入力）: エンコーダ位置、`PSH` / `BAK` / `CON` の押下状態と押下回数、BMP280 の温度、DHT の湿度、I2C デバイス数
- 画面 2（I2C）: 検出した全アドレスとデバイス名
- 画面 3（センサー）: BMP280 のチップ ID・温度・気圧、DHT の種類・湿度・温度
- `BAK` でページ送り、`CON` で I2C 再スキャン（どのボタンも押下回数に数えられます）
- すべての入力イベントをシリアルへ即時出力（`input: PSH press count=3` など）

| コマンド | 動作 |
| --- | --- |
| `scan` | I2C バスを再スキャン |
| `sensors` | BMP280 / DHT / OLED の状態 |
| `inputs` | エンコーダ位置と各ボタンの押下回数 |

## Release アセット

同一タグの Release に、両ファームウェアの書き込み用ファイルとマニフェストを添付します。

| ファイル | 内容 |
| --- | --- |
| `esp32-devkitc-station-merged.bin` | station 用の結合イメージ（`0x0000` から一発書き込み） |
| `esp32-devkitc-station-app.bin` | station のアプリ単体（`0x10000`） |
| `flash-manifest.json` | station のマニフェスト（**既定**。書き込みアプリはこれを自動で読みます） |
| `esp32-devkitc-tester-merged.bin` | tester 用の結合イメージ（`0x0000`） |
| `esp32-devkitc-tester-app.bin` | tester のアプリ単体（`0x10000`） |
| `tester-flash-manifest.json` | tester のマニフェスト |

`flash-manifest.json` / `tester-flash-manifest.json` には `variant`（`station` / `tester`）と `variantLabel` が入ります。station のマニフェストだけは、書き込みアプリがログ領域を案内できるよう `filesystems` を持ちます。オフセットとサイズは `scripts/build_release_bins.py` が `no_ota.csv` から自動で読み取ります。

```json
"filesystems": [
  {
    "type": "littlefs",
    "label": "spiffs",
    "offset": "0x210000",
    "size": 1966080,
    "mountPoint": "/littlefs",
    "logFile": "/log.csv"
  }
]
```

> パーティションを変更するときは `no_ota.csv` と `platformio.ini` の `board_build.partitions` を揃えてください。マニフェストの `filesystems` は同じ CSV から生成されるため、手で書き換える必要はありません。

> `esp32-devkitc-flasher` v0.2.0 以降はバリアント選択に対応します。それ以前のバージョンでは `flash-manifest.json`（station）が自動選択されるため、tester を書き込むときは Release の `.bin` を直接選んでください。

## 開発

### 必要環境
- PlatformIO Core（`pip install platformio`）
- Python 3.9 以上（`scripts/build_release_bins.py` / `tests/python` 用）

### ビルドとテスト
```bash
pio test -e native                  # ホスト環境のユニットテスト（212 ケース）
pio run -e esp32dev_station         # 多機能環境ステーション
pio run -e esp32dev_tester          # パーツ動作確認
pio run -e esp32dev_station -t upload
pio device monitor                  # 115200 bps
python3 -m unittest discover -s tests/python   # リリース生成・パーティション・pins.json のテスト
```

### リリース用バイナリの生成
```bash
FW_VERSION=0.3.0 pio run -e esp32dev_station
FW_VERSION=0.3.0 pio run -e esp32dev_tester
python scripts/build_release_bins.py \
  --build-dir .pio/build/esp32dev_station \
  --output-dir dist/station --version 0.3.0 --variant station
python scripts/build_release_bins.py \
  --build-dir .pio/build/esp32dev_tester \
  --output-dir dist/tester --version 0.3.0 --variant tester
```
`--variant` を省略すると従来（v0.1.x）の `esp32-devkitc-firmware-*.bin` / `flash-manifest.json` という名前で出力します。生成後はオフセットと SHA-256 が自動検証され、`--verify-only --variant station` で再検証だけ行えます。

### バージョンの埋め込み
`FW_VERSION=0.3.0 pio run -e esp32dev_station` のように環境変数で指定します。未指定時は `src/app_config.h` の既定値（`0.3.0`）が使われます。

### CI / Release
`.github/workflows/release-firmware.yml`（`docs/ci/release-firmware.yml` と同一）が以下を実行します。

- PR / `main` push: `pio test -e native` → Python テスト → station と tester の両方をビルド → 結合バイナリ生成と検証 → アーティファクト保存
- タグ `v*` push、または `workflow_dispatch`（`publish: true`）: 6 アセットを添付した GitHub Release を作成（既存 Release にはアップロード）

> **ワークフローを変更するときの注意**
> GitHub App の権限では `.github/workflows/` への push が 403 で拒否されます。ワークフローを変えるときは `docs/ci/release-firmware.yml` を更新し、その内容をブラウザ（`@yuushi2024` のセッション）で `.github/workflows/release-firmware.yml` へ反映してください。v0.2.0 の 2 バリアント対応もこの手順で反映済みです（反映前のタグビルドは v0.1.x の単一ファームウェアのままでした）。

### 結合バイナリのレイアウト
| オフセット | 内容 |
| --- | --- |
| `0x1000` | `bootloader.bin` |
| `0x8000` | `partitions.bin` |
| `0xe000` | `boot_app0.bin` |
| `0x10000` | `firmware.bin`（= `*-app.bin`） |

`-merged.bin` は `0x0000` から書き込めるよう `0xFF` でパディングした結合イメージで、`esptool.py merge_bin` の出力とバイト単位で一致します。

## ディレクトリ構成

```
src/
├── main.cpp                  # 起動・シリアル共通コマンド（station / tester を切り替え）
├── app_config.h              # ピン・周期・バージョン
├── command_handler.*         # コマンド解析と表示フォーマット（純C++）
├── pinout.*                  # pins.json から生成した配線表
├── i2c_transport.h           # I2C 抽象（実機とテストで共通）
├── i2c_helpers.cpp
├── i2c_scan.*                # バススキャンとデバイス判定
├── ble/                      # BLE GATT/HID のプロトコル定義とエンコード（純C++）
├── graphics/                 # QR コード生成・描画、ミニグラフ（純C++）
├── net/                      # Wi-Fi 認証情報・フォーム解析、NTP 時計、Web ダッシュボード（純C++）
├── sensors/                  # BMP280 ドライバ、DHT デコード、不快指数、スナップショット
├── display/                  # 5x7 フォント、フレームバッファ、SSD1306、ページ描画
├── input/                    # ロータリーエンコーダ、ボタン（デバウンス/長押し）
├── logging/                  # CSV フォーマットと記録状態（LogRecorder）
├── station/                  # station のページ生成・状態機械・タイマー・アラート・アプリ本体
├── tester/                   # tester のページ生成・レポート・アプリ本体
├── hal/                      # Arduino 依存（Wire / DHT GPIO / LittleFS / GPIO 入力 / Wi-Fi / Web / BLE）
└── test_support/             # テスト用フェイク（ファームウェアからは未使用）
```

リポジトリ直下の `no_ota.csv` がパーティション定義（app 2MB + LittleFS 1.9MB）、`pins.json` が配線の唯一の情報源です。詳細は [SPEC.md](./SPEC.md) を参照してください。
