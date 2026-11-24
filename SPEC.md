# esp32-devkitc-firmware 設計仕様書 (SPEC) — v0.2.0

## 1. 目的
ESP32-DevKitC 用のファームウェアを GitHub 上で管理・自動ビルドし、GitHub Releases に `.bin` を公開することで、PC を使わずに専用 Android アプリ（`yuushi-labs/esp32-devkitc-flasher`）から直接書き込めるようにする。

v0.2.0 では 1 リポジトリ・1 コードベースから **2 つのビルドターゲット** を提供する。

| ターゲット | 環境 | 目的 |
| --- | --- | --- |
| `station`（🎛️ 環境ステーション） | `esp32dev_station` | 温度・気圧・湿度を表示し、LittleFS へ CSV 記録。エンコーダとボタンで操作 |
| `tester`（🧪 パーツ動作確認） | `esp32dev_tester` | I2C 全スキャン、BMP280 / DHT の応答、エンコーダ・ボタン導通を表示 |

## 2. ハードウェア仕様
- ボード: **ESP32-DevKitC**（PlatformIO ボード ID: `esp32dev`）
- MCU: ESP32（Xtensa Dual-Core 32-bit LX6）、フラッシュ 4MB、`dio` / `40m`
- オンボード LED: GPIO2
- UART: 115200 bps
- パーティション: Arduino 標準 `default.csv`（LittleFS は `spiffs` 領域 `0x290000` / 1,441,792 B）

### 2.1 ピン割り当て
`pins.json` が唯一の情報源。ファームウェアとマニフェストは同じファイルから生成される。

| GPIO | 役割 | 方向 | 備考 |
| --- | --- | --- | --- |
| 21 | I2C SDA（OLED + BMP280） | input_output | 並列接続 |
| 22 | I2C SCL（OLED + BMP280） | input_output | 並列接続 |
| 32 | エンコーダ A 相 (TRA) | input_pullup | 入力専用ピン（内部プルアップあり） |
| 33 | エンコーダ B 相 (TRB) | input_pullup | 同上 |
| 25 | エンコーダ押し込み (PSH) | input_pullup / LOW | 記録間隔切替 |
| 26 | 戻る (BAK) | input_pullup / LOW | 前ページ |
| 27 | 決定 (CON) | input_pullup / LOW | 短押し記録 / 長押し記録ON-OFF |
| 4 | DHT DATA | input_output | 10kΩ で 3.3V へプルアップ |
| 2 | LED | output / HIGH | 1 秒点滅 |
| 0 | BOOT | input_pullup / LOW | 書き込みモード |

- 3.3V ロジック専用（5V 入力禁止）。OLED・BMP280・DHT の VCC は 3.3V で共通。
- GPIO34〜39 は入力専用かつ内部プルアップなしのため、エンコーダ／ボタンには使わない。
- GPIO6〜11 は内蔵フラッシュ専用。

### 2.2 I2C デバイス
| デバイス | アドレス | 判定方法 |
| --- | --- | --- |
| SSD1306 OLED 128x64 | `0x3C` → `0x3D` の順に探索 | ACK 応答 |
| BMP280 / BME280 | `0x76` → `0x77` の順に探索 | レジスタ `0xD0` のチップ ID（`0x58` / `0x60`） |

I2C は 100 kHz。`Wire.setTimeOut(50)` で未接続デバイスによるブロックを避ける。

## 3. リポジトリ構成
```
.
├── platformio.ini                 # esp32dev_station / esp32dev_tester / native
├── pins.json                      # GPIO 配線の定義（唯一の情報源）
├── src/
│   ├── main.cpp                   # 起動、共通シリアルコマンド、ターゲット振り分け
│   ├── app_config.h               # ピン・周期・バージョン・ターゲット判定
│   ├── command_handler.h/.cpp     # コマンド解析・help・banner/info/heartbeat
│   ├── pinout.h/.cpp              # pins.json から生成した配線表の表示
│   ├── i2c_transport.h            # I2C 抽象インターフェース
│   ├── i2c_helpers.cpp            # レジスタ読み書きヘルパー
│   ├── i2c_scan.h/.cpp            # バス全スキャン・デバイス判定・整形
│   ├── sensors/bmp280.h/.cpp      # BMP280/BME280 ドライバ（較正・補正演算）
│   ├── sensors/dht.h/.cpp         # DHT フレームデコード・自動判定・妥当性
│   ├── sensors/sensor_snapshot.h  # 表示・記録で共有する最新値
│   ├── display/font5x7.h          # 5x7 ビットマップフォント
│   ├── display/framebuffer.*      # 128x64 フレームバッファと文字描画
│   ├── display/ssd1306.*          # 初期化シーケンスとページ転送
│   ├── display/page_lines.*       # 行テキスト→ピクセル
│   ├── input/rotary_encoder.*     # 直交デコード（4 遷移 = 1 クリック）
│   ├── input/button.*             # デバウンス・長押し
│   ├── logging/log_record.*       # CSV 整形・LogStorage/LogRecorder
│   ├── station/station_model.*    # 4 ページの状態機械・ページ生成・レポート
│   ├── station/station_app.*      # station の Arduino 実装
│   ├── tester/tester_model.*      # tester のページ生成・レポート
│   ├── tester/tester_app.*        # tester の Arduino 実装
│   ├── hal/wire_i2c.*             # Wire による I2cTransport 実装
│   ├── hal/dht_gpio.*             # DHT のビットバンギング
│   ├── hal/littlefs_storage.*     # LittleFS による LogStorage 実装
│   ├── hal/gpio_inputs.*          # ピン読み取り→エンコーダ／ボタン
│   └── test_support/fake_i2c.h    # テスト用フェイク（ファームは未使用）
├── test/                          # PlatformIO native テスト（11 スイート / 131 ケース）
├── tests/python/                  # pins.json と flash-manifest 生成の unittest
├── scripts/                       # リリース生成・ヘッダ生成・バージョン埋め込み
├── docs/ci/release-firmware.yml   # .github/workflows/release-firmware.yml の控え
└── README.md / SPEC.md
```

## 4. 共通機能
1. 起動バナー: バージョン・ボード・チップモデル・チップ ID・リセット理由・`target=station|tester`
2. LED（GPIO2）: 起動時 1 秒周期点滅、`led on/off/blink` で切替
3. ハートビート: 5 秒ごとに `uptime_ms`, `free_heap`, `led_mode`
4. シリアルコマンド: `help`, `ping`, `info`, `pinout`, `led on|off|blink` に加え、ターゲット別コマンド（§5.3 / §6）
5. コマンドは `\n` / `\r\n` 区切り、前後空白と大文字小文字を無視。64 文字超は明示エラー

## 5. 🎛️ station（環境ステーション）

### 5.1 画面（OLED 128x64、8 行）
| # | ページ | 内容 |
| --- | --- | --- |
| 1 | サマリー | タイトル、温度（2 倍角）、気圧、湿度＋センサー種別、`REC <件数> n<秒>s` または `PAUSED <件数> rec` |
| 2 | BMP280 詳細 | チップ名とアドレス、温度、気圧、気圧トレンド、DHT の湿度・温度、ポーリング周期 |
| 3 | ログ管理 | 保存先、記録 ON/OFF、件数、記録間隔、次回まで、サイズ、`/log.csv` |
| 4 | システム | ファームバージョン、稼働時間、空きヒープ、I2C アドレス一覧、OLED、BMP280、ログ状態 |

未接続のセンサーは `--.-` / `not found` 等で表示し、他のページは通常どおり動作する（フリーズしない）。

### 5.2 入力
| 入力 | 動作 |
| --- | --- |
| エンコーダ回転（TRA/TRB） | ページ送り（1 クリック = 1 ページ） |
| PSH | 記録間隔 10 秒 → 1 分 → 5 分 を巡回 |
| BAK | 前のページ |
| CON 短押し（押して離す） | 手動で 1 件記録 |
| CON 長押し（0.9 秒以上） | 記録 ON / OFF の切替 |

長押しと短押しを取り違えないよう、記録は「離した時点で長押しが成立していない」場合だけ行う。

### 5.3 シリアルコマンド
`scan`, `sensors`, `log`, `log on`, `log off`, `log now`, `log clear`, `page`, `page 1..4`。

### 5.4 LittleFS ログ
- マウント: `LittleFS.begin(true)`（未フォーマット時は自動フォーマット）
- パス: `/log.csv`
- 形式: ヘッダー `uptime_ms,temp_c,pressure_hpa,humidity_pct`、以降 1 行 1 サンプル
- 温度は BMP280 を優先し、未接続時は DHT の温度を使う
- 欠測列は空欄（列数は固定）
- 記録間隔は `kLogIntervalsMs = {10000, 60000, 300000}`。`CON`/`PSH` の操作は即座に次回判定へ反映
- 件数はファイル内のヘッダー以外の行数。起動時に数え直す
- 書き込み失敗時は `log: write failed` を出し、記録状態は維持（再試行可能）
- マニフェストの `filesystems` に `spiffs` パーティション（`0x290000`, 1,441,792 B）を記載し、書き込みアプリの吸い出し機能と対応する

### 5.5 センサー異常時の挙動
| 状態 | 挙動 |
| --- | --- |
| BMP280 未接続 | 10 秒ごとに再探索。表示は `not found`、ログの温度・気圧列は空欄 |
| BMP280 BME280 以外のチップ ID | `unexpected chip id` として扱い、再探索を継続 |
| DHT 未接続 | 2.5 秒ごとに再試行。`humidity: none`。ログの湿度列は空欄 |
| DHT は応答するがフレーム不正 | `no answer` / `sensor answered but no valid frame` |
| OLED 未接続 | シリアルだけで全機能継続 |
| LittleFS マウント失敗 | 記録以外は動作継続。`log: unavailable` |

## 6. 🧪 tester（パーツ動作確認）

- 起動時と 5 秒ごとに I2C（`0x01`〜`0x7F`）を全スキャンし、アドレスとデバイス名を表示（最大 24 件）
- BMP280 は `0x76`/`0x77` を探索してチップ ID を表示。温度・気圧を 2 秒ごとに更新
- DHT は 2.5 秒ごとに読み取り、DHT11 / DHT22 を自動判定
- エンコーダ位置と PSH / BAK / CON の押下状態・回数をリアルタイム表示し、イベントをシリアルへ即時出力
- 画面: ①入力 ②I2C スキャン一覧 ③センサー詳細。`BAK` でページ送り、`CON` で再スキャン
- コマンド: `scan`, `sensors`, `inputs`
- どのパーツが欠けていても停止せず、残りで動作を継続する

## 7. テスト
- `pio test -e native`: 131 ケース。Arduino 非依存の全ロジック（コマンド解析、配線表示、I2C スキャン、BMP280 較正と補正演算、DHT デコード、フレームバッファと SSD1306 転送、エンコーダ／ボタン、CSV と LogRecorder、station/tester のページとレポート）を検証
- BMP280 の補正演算は BME280 データシートの補正例（25.08 °C / 100653 Pa）と一致することを確認
- `python3 -m unittest discover -s tests/python`: 52 ケース。`pins.json` のスキーマ検証、マニフェスト生成、バリアント命名、`filesystems` ブロック、後方互換を検証

## 8. Release 用バイナリ出力仕様
`scripts/build_release_bins.py --variant <station|tester>` が以下を出力する。

| ファイル | オフセット | 内容 |
| --- | --- | --- |
| `esp32-devkitc-<variant>-merged.bin` | `0x0000` | bootloader（`0x1000`）+ partitions（`0x8000`）+ boot_app0（`0xe000`）+ app（`0x10000`）を `0xFF` 埋めで結合 |
| `esp32-devkitc-<variant>-app.bin` | `0x10000` | アプリ単体 |
| `flash-manifest.json`（station） / `flash-manifest-tester.json`（tester） | — | オフセット・サイズ・SHA-256・バージョン・`variant`・`pins`・`wiringNotes`・`filesystems`（station のみ） |

- 生成後は自動で全ハッシュとオフセットを再検証する。`--verify-only --variant station` で再検証のみ可能
- `--variant` 省略時は v0.1.x 互換の `esp32-devkitc-firmware-*.bin` / `flash-manifest.json` を出力

## 9. CI / Release 自動化要件
- **PR / push 検証**: `pio test -e native` → Python テスト → `pio run -e esp32dev_station` と `-e esp32dev_tester` → 両方の結合バイナリ生成と検証 → アーティファクト保存
- **Release 自動作成**: タグ `v*` push、または `workflow_dispatch`（`publish: true`）で 6 アセットを添付した GitHub Release を公開。マニフェストの `build.gitCommit` とタグの指すコミットが一致することを確認してから公開する
- `.github/workflows/` の変更が GitHub App 権限で push できない場合は `docs/ci/` に同一内容を置き、ブラウザ経由で反映する
