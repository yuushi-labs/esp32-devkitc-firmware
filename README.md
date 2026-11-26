# esp32-devkitc-firmware

ESP32-DevKitC 向けのファームウェアリポジトリです。1 つのコードベースから **2 種類のファームウェア** をビルドし、GitHub Actions / Release で書き込み用 `.bin` を配布します。専用 Android アプリ [`yuushi-labs/esp32-devkitc-flasher`](https://github.com/yuushi-labs/esp32-devkitc-flasher) から USB OTG 経由でそのまま書き込めます。

| ターゲット | PlatformIO 環境 | 用途 |
| --- | --- | --- |
| 🎛️ `station`（多機能環境ステーション） | `esp32dev_station` | 起動画面の **VirtualPet（🐱 ドット絵のネコ・毎正時の時計お知らせ）**、温度・気圧・湿度・不快指数の表示、OLED ミニグラフ、NTP 時刻付き LittleFS 記録、Wi-Fi Web ダッシュボード＆QR、**Web からの OLED リモコン（画面操作・ON/OFF）**、BLE センサー配信＆メディアリモコン、物理タイマー／ポモドーロ |
| 🧪 `tester`（パーツ動作確認） | `esp32dev_tester` | I2C バス全スキャン、BMP280 / DHT の応答確認、エンコーダ・3 ボタンの導通を OLED とシリアルにリアルタイム表示（v0.2.0 相当） |

v0.4.1 でも **配線は v0.2.0 から変更ありません**（`pins.json` の 10 本もそのまま）。`tester` で配線とパーツを確認してから `station` を書き込む、という使い方を想定しています。

v0.4.1 の変更点:
- **Wi-Fi 画面の QR から戻れないバグを修正**しました。`4 WIFI & WEB` → `CON`（QR）→ `BAK` で QR が閉じて Wi-Fi 画面に戻り、もう一度 `BAK` でメニューへ戻ります。次に `4` を開いたときは QR ではなく Wi-Fi ステータス画面から始まります
- **Wi-Fi の設定を LittleFS の `/wifi.json` に保存**するようになりました（従来の NVS 設定は初回起動時に自動で `/wifi.json` へ移行）。`PSH` で AP モードに入り直しても設定が残り、`wifi status` と `/api/status` で保存先（`littlefs` / `nvs`）が分かります
- **ネコ「VirtualPet」の動きが増えました**。歩く・座るに加えて、伸び・ぴょんっ（ジャンプ）・毛づくろい・キョロキョロ・うたた寝（あくび付き）を時間で切り替えます。さらにペット画面で**エンコーダを回すか `PSH` を押すと、ネコが喜んで跳ね、`♥` `♪` `✦` が弾けます**
- **毎正時の時計お知らせが「お祝い」になりました**。時計を持って `HH:00` を知らせる 8 秒間、ネコはうれしそうにぴょんぴょん跳ね、しっぽを速く振り、`♥` `♪` `✦` が周りに浮かびます（`CON` / `pet clock` / Web の「Clock chime」でも同じ演出）

v0.4.0 の変更点:
- **起動画面が VirtualPet（🐱）**になりました。ネコが歩いたり座ったりまばたきしたり、夜は眠ります。**毎正時（`HH:00`）には時計を持ってきて時刻を知らせます**（`CON` 短押し・シリアル `pet clock`・Web の「Clock chime」でいつでも再演奏できます）
- **`BAK` を押すと、どの画面からでもこれまでと同じホームメニューが開きます**（ペット画面では短押し・長押しどちらでも）。メニューは `1 PET CAT` 〜 `7 SYSTEM` の 7 項目で、エンコーダで選んで `CON`/`PSH` で入ります
- **AP 設定モード中の QR が Wi-Fi 参加用**（`WIFI:T:WPA;S:ESP32-Station-AP;P:...;;`）になり、読み取るだけでスマホが設定用 AP に参加できます。接続中の QR は従来どおりダッシュボードの URL です
- **Web ダッシュボードに「OLED remote」**を追加。画面 ON/OFF、Pet / Monitor / Graph / Detail / Logs / Wi-Fi / BLE / Timer / System / Menu への直接切り替え、`Back`/`Prev`/`Next`/`Select`、時計演出のテスト再生ができます

## ターゲット環境
- **ボード**: ESP32-DevKitC（`esp32dev` / 無印 ESP32、4MB フラッシュ）
- **ビルドシステム**: PlatformIO（`espressif32@6.9.0`、Arduino framework）
- **シリアル通信**: 115200 bps
- **外部ライブラリ**: [`h2zero/NimBLE-Arduino`](https://github.com/h2zero/NimBLE-Arduino)（BLE 用）のみ。OLED・BMP280・DHT・QR コードは本リポジトリ内で実装
- **パーティション**: `no_ota.csv`（app 2MB + LittleFS 約 1.9MB）。Wi-Fi + Web サーバー + BLE を同時に積むと `default.csv` の 1.25MB アプリ枠に収まらないため、OTA を外してアプリ領域を広げています。書き込みは USB 経由の Android アプリで行います
- **バージョン**: `src/app_config.h` の既定値は `0.4.0`。リリースビルドでは `FW_VERSION` で上書きします

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
- オンボード LED（GPIO2）: `station` は**消灯で起動**（机の上で点滅し続けない）、`tester` は 1 秒点滅。`led on` / `led blink` でいつでも切り替えできます
- 5 秒ごとの `heartbeat uptime_ms=... free_heap=... led_mode=...`
- 共通コマンド: `help` / `ping` / `info` / `pinout` / `led on|off|blink`

## 🎛️ station（多機能環境ステーション）

**起動直後は VirtualPet（🐱）の画面**です。`BAK` を押すとこれまでと同じホームメニューが開きます（ペット画面では短押し・長押しどちらでも）。メニューはエンコーダで選び、`CON` または `PSH` で入ります。どのモードからでも `BAK` 長押しでホームへ戻れます。

| # | モード | 内容 |
| --- | --- | --- |
| 1 | 🐱 VirtualPet | 起動画面。ネコが歩く・座る・まばたき・夜は眠る。温湿度に応じたひとこと付き。**毎正時に時計を持って `HH:00` を知らせる**。`CON` で時計演出を再演奏、`BAK` でメニュー |
| 2 | 📊 環境モニター | 温度（BMP280 優先、未接続時は DHT）・気圧・湿度・不快指数・天気傾向・記録状態。`CON` でサマリー → グラフ → 詳細を切替 |
| 3 | 📁 ログ管理 | NTP 時刻付き CSV の ON/OFF、記録間隔、件数、次回まで、ファイルサイズ |
| 4 | 🌐 Wi-Fi ＆ Web | 接続状態・IP・RSSI・NTP 時刻。未設定時は AP 設定モード。`CON` で QR コード表示 |
| 5 | 📶 Bluetooth | GATT センサー配信とメディアリモコン（HID）モードの切替・接続状態 |
| 6 | ⏱️ タイマー | 1〜60 分カウントダウン／ポモドーロ（25 分作業・5 分休憩） |
| 7 | 🛠️ システム | バージョン・稼働時間・空きヒープ・I2C アドレス・OLED・Wi-Fi・BLE・ログ状態 |

### VirtualPet（起動画面）
- 128x64 パネルに「時計（左上）・気分（右上）・ネコ・地面・センサー値」を配置します。ネコは 24x20 px の 1 ビットビットマップ（`src/pet/pet_sprites.h`）で、尾は 3 フレーム、目・鼻・肉球はビットを抜いて描きます
- **日中のおしごと**（101.4 秒でひと巡り）: 歩く（18 px/s で左右往復＋1 px バウンド）と座るを基本に、**伸び（3 秒）→ 毛づくろい（4 秒）→ ぴょんっ（2.4 秒）→ キョロキョロ（4 秒）→ うたた寝（8 秒・最初にあくび）** をはさみます。しっぽは 400 ms ごとに揺れ、3.2 秒ごとに 0.18 秒まばたきします。23:00〜05:59（NTP 同期時）は目を閉じて眠り `z` を出します
- **なでる**: ペット画面でエンコーダを回すか `PSH` を押すと、ネコが跳ねて `♥` `♪` `✦` を出し、`yay!` / `purr!` と鳴きます（2.4 秒）。`BAK`＝メニュー、`CON`＝時計演出のテスト、という既存操作はそのままです
- 気分は温度・湿度から `hot!` / `cold` / `dry!` / `humid` / `warm` / `nice` を決め、右上とひとこと（`so hot...` など）に出します
- **毎正時**: 分 `00`・秒 0〜19 の最初の更新で 1 時間に 1 回だけ、上部に吹き出しを出して時計アイコンと `07:00 !` を 2 倍角で 8 秒表示します。この 8 秒間は**ネコが喜んで跳ね、しっぽを速く振り、まわりに `♥` `♪` `✦` が浮かびます**。同じ時刻の二重発火は `(月,日,時)` で防止し、翌日の同じ時刻にはまた鳴ります（NTP 未同期では鳴りません）
- ネコ画面だけ 250 ms 間隔で再描画します（他の画面は 500 ms）。ピクセルが変わらないフレームは I2C に送りません

### OLED の画面 ON/OFF（自動復帰）
- SSD1306 の `DISPLAYOFF` / `DISPLAYON` で本当にパネルを消灯・点灯します。OFF 中は描画こそ続きますが **I2C 転送は 0** です
- **暗いときに物理ボタンかエンコーダを触ると自動で点灯**します。点灯のきっかけになった操作は飲み込むので、暗い画面を触った拍子にモードが変わることはありません
- Web の「Screen off / Screen on」、シリアル `oled on` / `oled off` / `oled status` でも切り替えられます

### 環境モニター
- 温度が 2 倍角フォントで大きく表示され、気圧・湿度・不快指数・天気傾向（sunny / cloudy / rain）を 1 画面にまとめます
- グラフ画面は直近 64 サンプル（10 秒間隔）の温度・気圧を 2 本のミニ折れ線で描画します
- 詳細画面は BMP280 のチップ名・アドレス、気圧トレンド、DHT の値、不快指数を表示します
- しきい値アラート: 高温 **30℃ 以上**、乾燥 **35% 以下**、気圧急変 **1.5 hPa 以上**。画面に `! HOT31.0 DRY30 PJMP` のように表示します（LED は `led blink` にしているときだけ高速点滅し、既定の消灯では光りません）

### ボタン操作（モード別）
| 操作 | VirtualPet | 環境モニター | ログ | Wi-Fi | BLE（HID モード） | タイマー |
| --- | --- | --- | --- | --- | --- | --- |
| エンコーダ回転 | なでる（喜ぶ） | サマリー↔グラフ↔詳細 | — | — | 音量 UP / DOWN | 1〜60 分を調整（停止中のみ） |
| `PSH` | なでる（喜ぶ） | 次の表示 | 記録間隔を切替（10 秒→1 分→5 分） | AP 設定モードを開始 | 再生 / 一時停止 | ポモドーロ切替 |
| `CON` 短押し | 時計演出を再演奏 | 次の表示 | 手動で 1 件記録 | QR コードを表示 | 次の曲 | 開始 / 一時停止 |
| `BAK` 短押し | **メニューを開く** | 前の表示 | 前の表示 | QR 表示中は QR を閉じる／もう一度でメニュー | 前の曲 | リセット |
| `CON` 長押し（0.9 秒） | — | — | 記録 ON / OFF | — | — | — |
| `BAK` 長押し（0.9 秒） | メニューを開く | ホームへ | ホームへ | ホームへ | ホームへ | ホームへ |

タイマーは 00:00 で OLED を反転点滅して知らせます（LED は `led blink` のときだけ一緒に点滅）。

### LittleFS ログ
- `LittleFS.begin(true)` でマウント（未フォーマットなら自動フォーマット）
- `/log.csv` のヘッダーは `timestamp,uptime_ms,temp_c,pressure_hpa,humidity_pct`
- Wi-Fi + NTP 同期時は `timestamp` に `YYYY-MM-DD HH:MM:SS`（JST `UTC+9`）を記録し、未同期時は空欄
- 未接続センサーの列は空欄（例: `,1234,23.45,,`）になり、列数は常に一定
- `no_ota.csv` の `spiffs` 領域（`0x210000` / 1,966,080 B）を使います。`esp32-devkitc-flasher` の LittleFS 吸い出しでそのまま `/log.csv` を読み出せます
- v0.2.0 の 4 列ログが残っている場合は、起動時に `/log-v020.csv` へ退避してから新しい 5 列ファイルを始めます（同じファイルに 2 種類の列構成を混ぜません）。退避先が既にあるときは `/log-v020-2.csv` 〜 `/log-v020-9.csv` の空き番号を使うので、以前の退避ログは消えません（すべて埋まっているときは旧ファイルを残して記録を停止します）

### Wi-Fi ＆ Web ダッシュボード
- **初回設定（AP モード）**: SSID `ESP32-Station-AP` は **WPA2 で保護**され、パスワードは起動ごとに生成されて OLED の Wi-Fi 画面に表示されます（周囲から設定用パスワードを盗み見られないようにするため）。接続して `http://192.168.4.1/` のフォームで周辺 SSID を選びパスワードを保存します。認証情報は **LittleFS の `/wifi.json`** に保存され（v0.4.0 以前の NVS 設定は初回起動時に自動移行）、次回起動時に自動接続します。`/wifi.json` が読めないときは NVS の設定で接続し、ファイルを書き直します。保存は一時ファイル `.tmp` に書いてから置き換えるので、書き込み中にリセットされても前の設定は残ります
- **設定フォームのトークン**: フォームに埋めた使い捨てトークンは**有効な間は使い回します**（ページ取得のたびに作り直さない）。ブラウザが裏で `/favicon.ico` や captive portal の確認を投げても、画面に出ているフォームのトークンが無効になることはありません。トークンは保存成功時と AP 終了時に破棄されます。`/favicon.ico` と `/robots.txt` は 204（本文なし）で返します
- **ダッシュボード**: `http://<ESP32 の IP>/` に外部 CDN 非依存の HTML/CSS/JS を配信します。温湿度・気圧・不快指数・Wi-Fi/BLE/ログ/タイマー状態を自動更新し、履歴グラフ・`/log.csv` ダウンロード・ログ消去を提供します
- **JSON API**: `GET /api/status`（現在値と各機能の状態）、`GET /api/logs`（直近 120 件の履歴）、`GET /download`（CSV）、`GET /api/oled`（OLED の電源・画面・モード・ペット状態）、`POST /api/oled`（`power=on|off|toggle`・`mode=...`・`view=summary|graph|detail`・`action=next|prev|select|back|clock`）
- **OLED remote**: ダッシュボードの「OLED remote」から、画面 ON/OFF、`Back`/`Prev`/`Next`/`Select`、`Clock chime`、そして Pet / Monitor / Graph / Detail / Logs / Wi-Fi / BLE / Timer / System / Menu への直接切り替えができます。現在の画面と電源状態は 2 秒ごとに更新されます
- **QR コード**: Wi-Fi モードで `CON` を押すか、ダッシュボードの「Show QR on OLED」を押すと QR コードを OLED に描画します。Wi-Fi 接続中は `http://<IP>/`、**AP 設定モード中は `WIFI:T:WPA;S:ESP32-Station-AP;P:<生成キー>;;`**（読み取るだけでスマホが設定用 AP に参加できます。`\` `;` `,` `:` `"` はエスケープ）を指します（内蔵エンコーダ、ISO/IEC 18004 byte mode・version 1〜10）。128x64 パネルでは白地に黒モジュール・1 モジュール 2 px（`WIFI:` 形式は 29 モジュールで 58x58 px、`http://` 形式は 25 モジュールで 50x50 px）で描き、外周 2 px の白フチとパネル自体の余白をクワイエットゾーンにします。収まらない大きさのときは QR の代わりに文字列を表示します
- 未設定・切断時も環境モニター・ログ・BLE・タイマーはオフラインで動作し、Wi-Fi はブロッキングせずバックグラウンドで再接続します。設定ページの周辺 SSID スキャンも非同期で、ページ応答やボタン操作を止めません

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
| `page` / `page 1..7` | 次のモード / 指定モードへ |
| `mode` / `mode NAME` | 現在の画面・モード表示 / `pet` `env` `logs` `wifi` `ble` `timer` `system` `menu` を開く |
| `oled` / `oled on` / `oled off` | OLED の電源状態 / 画面を点灯・消灯（ボタンを触ると自動点灯） |
| `pet` / `pet clock` | ペットの状態表示 / 毎正時の時計演出をテスト再生 |
| `wifi` / `wifi ap` / `wifi station` / `wifi clear` | Wi-Fi・NTP 状態 / AP 設定モード開始 / 保存済みネットワークへ再接続 / 保存済み認証情報を消去 |
| `ble` / `ble hid on` / `ble hid off` | BLE 状態 / メディアリモコンモード ON・OFF |
| `timer` / `timer N` / `timer start\|pause\|reset` | タイマー状態 / 1〜60 分設定 / 開始・一時停止・リセット |

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
pio test -e native                  # ホスト環境のユニットテスト（すべてのロジックを網羅）
pio run -e esp32dev_station         # 多機能環境ステーション
pio run -e esp32dev_tester          # パーツ動作確認
pio run -e esp32dev_station -t upload
pio device monitor                  # 115200 bps
python3 -m unittest discover -s tests/python   # リリース生成・パーティション・pins.json のテスト
```

### リリース用バイナリの生成
```bash
FW_VERSION=0.4.1 pio run -e esp32dev_station
FW_VERSION=0.4.1 pio run -e esp32dev_tester
python scripts/build_release_bins.py \
  --build-dir .pio/build/esp32dev_station \
  --output-dir dist/station --version 0.4.1 --variant station
python scripts/build_release_bins.py \
  --build-dir .pio/build/esp32dev_tester \
  --output-dir dist/tester --version 0.4.1 --variant tester
```
`--variant` を省略すると従来（v0.1.x）の `esp32-devkitc-firmware-*.bin` / `flash-manifest.json` という名前で出力します。生成後はオフセットと SHA-256 が自動検証され、`--verify-only --variant station` で再検証だけ行えます。

### バージョンの埋め込み
`FW_VERSION=0.4.1 pio run -e esp32dev_station` のように環境変数で指定します。未指定時は `src/app_config.h` の既定値（`0.4.1`）が使われます。

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
├── net/                      # Wi-Fi 認証情報・フォーム解析・Wi-Fi QR ペイロード、NTP 時計、Web ダッシュボード（純C++）
├── pet/                      # VirtualPet: ネコのビットマップ・状態機械・毎正時トリガー・画面描画（純C++）
├── sensors/                  # BMP280 ドライバ、DHT デコード、不快指数、スナップショット
├── display/                  # 5x7 フォント、フレームバッファ、SSD1306、ページ描画
├── input/                    # ロータリーエンコーダ、ボタン（デバウンス/長押し）
├── logging/                  # CSV フォーマットと記録状態（LogRecorder）
├── station/                  # station のページ生成・状態機械・タイマー・アラート・OLED リモコン・アプリ本体
├── tester/                   # tester のページ生成・レポート・アプリ本体
├── hal/                      # Arduino 依存（Wire / DHT GPIO / LittleFS / GPIO 入力 / Wi-Fi / Web / BLE）
└── test_support/             # テスト用フェイク（ファームウェアからは未使用）
```

リポジトリ直下の `no_ota.csv` がパーティション定義（app 2MB + LittleFS 1.9MB）、`pins.json` が配線の唯一の情報源です。詳細は [SPEC.md](./SPEC.md) を参照してください。
