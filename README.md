# esp32-devkitc-firmware

ESP32-DevKitC 向けのファームウェアリポジトリです。1 つのコードベースから **2 種類のファームウェア** をビルドし、GitHub Actions / Release で書き込み用 `.bin` を配布します。専用 Android アプリ [`yuushi-labs/esp32-devkitc-flasher`](https://github.com/yuushi-labs/esp32-devkitc-flasher) から USB OTG 経由でそのまま書き込めます。

| ターゲット | PlatformIO 環境 | 用途 |
| --- | --- | --- |
| 🎛️ `station`（環境ステーション） | `esp32dev_station` | OLED に温度・気圧・湿度・記録状態を表示し、エンコーダ／ボタンでページ操作。LittleFS に `/log.csv` を記録 |
| 🧪 `tester`（パーツ動作確認） | `esp32dev_tester` | I2C バス全スキャン、BMP280 / DHT の応答確認、エンコーダ・3 ボタンの導通を OLED とシリアルにリアルタイム表示 |

`tester` で配線とパーツを確認してから `station` を書き込む、という使い方を想定しています。

## ターゲット環境
- **ボード**: ESP32-DevKitC（`esp32dev` / 無印 ESP32、4MB フラッシュ）
- **ビルドシステム**: PlatformIO（`espressif32@6.9.0`、Arduino framework）
- **シリアル通信**: 115200 bps
- **外部ライブラリ**: なし（OLED・BMP280 ドライバは本リポジトリ内で実装）

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

## 🎛️ station（環境ステーション）

### 画面（エンコーダ回転で切替）
1. **現在値サマリー**: 温度（大きく表示）・気圧・湿度・記録中かどうかと次回記録まで
2. **BMP280 詳細**: チップ名とアドレス・温度・気圧・気圧トレンド（rising / steady / falling）・DHT の値・ポーリング周期
3. **LittleFS ログ管理**: 記録 ON/OFF・件数・記録間隔・次回まで・ファイルサイズ・`/log.csv`
4. **システム / I2C 状態**: ファームバージョン・稼働時間・空きヒープ・I2C で見つかったアドレス・OLED・BMP280・ログ

### ボタン操作
| 操作 | 動作 |
| --- | --- |
| エンコーダ回転 | ページ送り / 戻し |
| `PSH` 押し込み | 記録間隔を切替（10 秒 → 1 分 → 5 分） |
| `BAK` | 前のページ |
| `CON` 短押し | 手動で 1 件記録 |
| `CON` 長押し（0.9 秒） | 記録 ON / OFF |

### LittleFS ログ
- `LittleFS.begin(true)` でマウント（未フォーマットなら自動フォーマット）
- `/log.csv` に `uptime_ms,temp_c,pressure_hpa,humidity_pct` を追記
- 接続されていないセンサーの列は空欄（例: `1234,23.45,,`）になり、列数は常に一定
- 1 行目はヘッダー。件数はヘッダーを除いた行数
- `spiffs` パーティション（`default.csv` の `0x290000` / 1,441,792 B）を使うため、`esp32-devkitc-flasher` の LittleFS 吸い出しでそのまま `/log.csv` を読み出せます

### シリアルコマンド（station）
| コマンド | 動作 |
| --- | --- |
| `scan` | I2C バスを再スキャンして一覧表示 |
| `sensors` | BMP280 / DHT の状態を表示 |
| `log` | ログ状態（保存先・ON/OFF・間隔・件数・サイズ） |
| `log on` / `log off` | 記録の開始 / 停止 |
| `log now` | 手動で 1 件記録 |
| `log clear` | `/log.csv` を削除してヘッダーだけ作り直す |
| `page` / `page 1..4` | 次のページ / 指定ページへ |

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

`flash-manifest.json` / `tester-flash-manifest.json` には `variant`（`station` / `tester`）と `variantLabel` が入ります。station のマニフェストだけは、書き込みアプリがログ領域を案内できるよう `filesystems` を持ちます。

```json
"filesystems": [
  {
    "type": "littlefs",
    "label": "spiffs",
    "offset": "0x290000",
    "size": 1441792,
    "mountPoint": "/littlefs",
    "logFile": "/log.csv"
  }
]
```

> `esp32-devkitc-flasher` v0.2.0 以降はバリアント選択に対応します。それ以前のバージョンでは `flash-manifest.json`（station）が自動選択されるため、tester を書き込むときは Release の `.bin` を直接選んでください。

## 開発

### 必要環境
- PlatformIO Core（`pip install platformio`）
- Python 3.9 以上（`scripts/build_release_bins.py` / `tests/python` 用）

### ビルドとテスト
```bash
pio test -e native                  # ホスト環境のユニットテスト（131 ケース）
pio run -e esp32dev_station         # 環境ステーション
pio run -e esp32dev_tester          # パーツ動作確認
pio run -e esp32dev_station -t upload
pio device monitor                  # 115200 bps
python3 -m unittest discover -s tests/python   # リリース生成・pins.json のテスト
```

### リリース用バイナリの生成
```bash
FW_VERSION=0.2.0 pio run -e esp32dev_station
FW_VERSION=0.2.0 pio run -e esp32dev_tester
python scripts/build_release_bins.py \
  --build-dir .pio/build/esp32dev_station \
  --output-dir dist/station --version 0.2.0 --variant station
python scripts/build_release_bins.py \
  --build-dir .pio/build/esp32dev_tester \
  --output-dir dist/tester --version 0.2.0 --variant tester
```
`--variant` を省略すると従来（v0.1.x）の `esp32-devkitc-firmware-*.bin` / `flash-manifest.json` という名前で出力します。生成後はオフセットと SHA-256 が自動検証され、`--verify-only --variant station` で再検証だけ行えます。

### バージョンの埋め込み
`FW_VERSION=0.2.0 pio run -e esp32dev_station` のように環境変数で指定します。未指定時は `src/app_config.h` の既定値（`0.2.0`）が使われます。

### CI / Release
`.github/workflows/release-firmware.yml`（`docs/ci/release-firmware.yml` と同一）が以下を実行します。

- PR / `main` push: `pio test -e native` → Python テスト → station と tester の両方をビルド → 結合バイナリ生成と検証 → アーティファクト保存
- タグ `v*` push、または `workflow_dispatch`（`publish: true`）: 6 アセットを添付した GitHub Release を作成（既存 Release にはアップロード）

> **ワークフローを変更するときの注意**
> GitHub App の権限では `.github/workflows/` への push が 403 で拒否されます。ワークフローを変えるときは `docs/ci/release-firmware.yml` を更新し、その内容をブラウザ（`@yuushi2024` のセッション）で `.github/workflows/release-firmware.yml` へ反映してください。v0.2.0 の 2 バリアント対応もこの手順で反映済みです（反映前のタグビルドは v0.1.x の単一ファームウェアのままでした）。

`env:esp32dev` は v0.1.x のワークフローが使っていた後方互換エイリアス（station と同じ設定）で、v0.3.0 で削除予定です。

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
├── sensors/                  # BMP280 ドライバ、DHT デコード、スナップショット
├── display/                  # 5x7 フォント、フレームバッファ、SSD1306、ページ描画
├── input/                    # ロータリーエンコーダ、ボタン（デバウンス/長押し）
├── logging/                  # CSV フォーマットと記録状態（LogRecorder）
├── station/                  # station のページ生成・状態機械・アプリ本体
├── tester/                   # tester のページ生成・レポート・アプリ本体
├── hal/                      # Arduino 依存（Wire / DHT GPIO / LittleFS / GPIO 入力）
└── test_support/             # テスト用フェイク（ファームウェアからは未使用）
```

詳細は [SPEC.md](./SPEC.md) を参照してください。
