# line-phantom-beacon

M5Stack / Seeed XIAO 系の ESP32 ボード 4〜5種に対応した、Wi-Fi CSI による人の動き検知(ESPectre)プロジェクト。
疑似イベント(動き検知 / 10秒・20秒・30秒無動作の3段階)を **LINE Messaging API べ直接 push(サーバーレス)** し、
オプションで **Grafana Cloud べの時系列テレメトリも送れる**。
LINE Simple Beacon 形式の BLE Advertising(Phase 2)はフレーム生成部まで実装済み。

⚠️ 本プロジェクトが生成する `pseudo_leave` / `sleep_suspected` は**完全に独自の疑似イベント**であり、LINE公式の `leave` イベントや LINEプラットフォームを偽装するものではありません。また、CSIによる動き検知は個人を識別せず「動きがない=退室」「動きがない=睡眠」も一切断定できません。

## 対応ボード(全 4 ボードで実機検証 or 動作確認済み)

| env | ボード | LCD/LED | CSI トラフィック | 状態 |
|---|---|---|---|---|
| `m5stack-atoms3`(既定) | **M5Stack AtomS3** | LCD 状態表示 | WIFI_RAW | ✅ 完全実機検証済み(3段階 push 込み) |
| `m5stack-atom` | **ATOM Echo/Matrix/Lite** | RGB LED + セビープ | WIFI_RAW | ✅ 実機検証済み(push到達)。セビープ音の音出しは未解決 |
| `seeed_xiao_esp32c3` | **XIAO ESP32-C3** | 単色 LED 点滅(GPIO3) | WIFI_RAW | ✅ 完全実機検証済み(CSI occ=1.0、LINE push到達) |
| `seeed_xiao_esp32c6` | **XIAO ESP32-C6** | 単色 LED 点滅(GPIO15) | **DNS**(C6 は WIFI_RAW 不可) | ✅ LINE push到達確認済み(2026-11-18) |
| `seeed_xiao_esp32s3` | XIAO ESP32-S3(+Plus/Sense) | 単色 LED 点滅 | WIFI_RAW | ⏳ 検証中(USB 列挙問題の解決待ち) |

- ビルド: `pio run -e <env>`(既定 = AtomS3)。ロジック・LINE push・しきい値(10/20/30秒)は全ボード同一実装(テスト 17/17)
- トラフィックモードは env の build_flags で選択: `PHANTOM_TRAFFIC_DNS` / `PHANTOM_TRAFFIC_PING` / 既定 WIFI_RAW
  (ESPectre 3.2.0 は **ESP32-C6 で WIFI_RAW をライブラリ側で拒否**するため、C6 は DNS で生成。occ≈0.9 を確認)

## 書き込みの要点(4MB ボードの初回に注意)

- ローカル PC の `pio run -t upload` は **bootloader+パーティション+アプリを全部書く**(これが推奨ルート)
- Codespaces(pio remote)から書く場合、**アプリ領域(0x10000)のみ**書かれるため、
  **4MB ボードの初回だけ** bootloader+パーティション込みの統合バイナリ直書きが必要
  (手順は各 handson 資料)。ボードに古い bootloader が残ったままだと、IROM≈1MB を越えるアプリの
  マッピングに失敗し **ログ無しの起動ループ**(`rst:0x3` 連発)になる(C3 rev v0.3 で実証・解決済み)
- 一度 bootloader+パーティションが入れば、以後は `pio remote run -t upload` で更新可能

## ドキュメント(ボード別ハンズオン 5 種)

- [docs/handson-junbi.md](docs/handson-junbi.md) — **共通準備ガイド**(LINE Bot作成・HWID・ESPectre解説・環境整備・「クラウド→ホスト→USB」の全体フロー図。まずこれ)
- [docs/handson-atoms3.md](docs/handson-atoms3.md) — **AtomS3 用**(LCDあり。Codespaces/ローカル両対応)
- [docs/handson-atom-echo.md](docs/handson-atom-echo.md) — **ATOM Echo/Matrix/Lite 用**(RGB LED・初回統合バイナリあり)
- [docs/handson-xiao-esp32c3.md](docs/handson-xiao-esp32c3.md) — **XIAO ESP32-C3 用**(初回 bootloader 更新必須)
- [docs/handson-xiao-esp32c6.md](docs/handson-xiao-esp32c6.md) — **XIAO ESP32-C6 用**(DNS トラフィック)
- [docs/readme.md](docs/readme.md) — **ボード別 検証の注意点・落とし穴(実記録)** ← 先に読むと詰まらない
- [docs/handsons-ouyou.md](docs/handsons-ouyou.md) — **応用コンテンツ: 感度とキャリブレーションの「どこをどう変えるとどうなるか」**(用途別プリセット・無人時誤検知の調査フロー)
- [docs/espectre-feature-survey.md](docs/espectre-feature-survey.md) — ESPectre 3.2.0 の実装済み/不可能/実装可能 機能調査
- [docs/blog.md](docs/blog.md) — ここまでの開発のまとめ(設計判断と落とし穴)

## システム構成

```
[ESP32 ボード] ==Wi-Fi CSI==> ESPectre 動き検知 ──状態遷移──┐
[ESP32 ボード] --HTTPS(疑似イベント直接 push)──────────────┴--> [LINE Messaging API] --> ユーザー
[ESP32 ボード] --HTTPS(テレメトリ:オプション)--> [Grafana Cloud Loki] --> ダッシュボード
[ESP32 ボード] --BLE adv (LINE Simple Beacon 形式, Phase 2)--> [スマホ] (未実装)
```

- ESP32 が ESPectre の動き検知履歴から擬似イベントを生成し、**LINE Messaging API へ直接 HTTPS push する(サーバーレス構成)**
- チャネルアクセストークンは `include/secrets.h`(gitignore済み)**に置き、Git にはコミットしない**。漏れたら LINE Developers コンソールでローテーション
- 同一イベント種別の再送抑制を ESP32 側で行う(LINE 無料メッセージ枠の保護)
- テレメトリ: 同じ secrets.h べの 3 行設定で Grafana Cloud の Loki ぼ直接 HTTPS push(未設定なら完全無効)

### モニタリング: Grafana Cloud へのテレメトリ送信(オプション)

LINE push に加えて、イベント・在室フラグ(presence)・5分ごとの状態(status)を **Grafana Cloud(無料
枠: ユーザー3人/ログ50GB月/保持10日)の Loki** に送り、**ダッシュボードで「部屋に人がいるか」が
時系列で見られる**。LINE の 429(月200通)に達してもテレメトリには全事件が記録される。
**有効化は secrets.h への `TELEMETRY_TOKEN` 設定だけ**で、`TELEMETRY_URL` / `TELEMETRY_LOKI_USER`
(それぞれスタック固有)も合わせて設定すれば送出される。ラベルは `job=line-phantom-beacon`、
`device=<端末ラベル>`、`kind=event/presence/status`。TLS 検証は `src/telemetry_root_ca.h`
(DigiCert Global Root G2 系・leaf は回転するため含めない)。

## 必要なハードウェア

- **M5Stack AtomS3**(`m5stack-atoms3`,既定・LCDに状態表示)
- 代替ボード: **ATOM Echo**(`m5stack-atom`)**XIAO ESP32-C3**(`seeed_xiao_esp32c3`)
  **XIAO ESP32-C6**(`seeed_xiao_esp32c6`)**XIAO ESP32-S3**(`seeed_xiao_esp32s3`)
- **2.4GHz の Wi-Fi 4 (802.11n) アクセスポイント**(5GHz専用では CSI が取れない。テザリング可)
- AtomS3 と AP を同一ネットワークに接続(ESPectre はゲートウェイへのトラフィックで CSI を汲む)

## Dockerでの開発方法 / PlatformIOのビルド方法

### 前提
- `shared-dev-environments` を `../../shared-dev-environments` に配置(または `SHARED_DEV_ENV_DIR` を指定)。
- 共通イメージ `shared-pio:latest` をビルド済みにする(`docker image ls shared-pio` で確認):
  `cd shared-dev-environments && npm run build:pio`

### ビルド
```bash
cd line-phantom-beacon
docker compose run --rm pio          # コンテナ内 /workspace で bash が開く
pio run                              # ビルド(既定 = atoms3)。初回はツールチェーン数GBで10分+
```

### ネイティブテスト(ボード不要。コンテナ内で完結)
```bash
pio test -e native                   # lib/phantom_events + lib/line_beacon_frame のテスト(17/17)
```

ビルド成果物は `.pio/build/<env>/`(ホストからも見える。Git管理外)。

## ESP32への書き込み・シリアルモニタ(macOS + OrbStack 実績あり)

**推奨ルート**: **ローカル PC の `pio run -t upload`**(bootloader 込みで焼ける。手順は handson 資料)。
コンテナ/Codespaces からは **pio remote**(コンテナでコマンド1つ、ホストに3GB不要):

**1. 初回だけ(ホスト側で)**
```bash
pio account login          # PlatformIO アカウント(対話ログイン)
pio remote agent start     # 常駐させる(ターミナルを閉じない)
```

**2. トークンを `.env` に追加してからコンテナで実行**
```bash
export PLATFORMIO_AUTH_TOKEN=$(sed -n 's/^PLATFORMIO_AUTH_TOKEN=//p' .env)
pio remote run -t upload          # ビルド(コンテナ) → クラウド経由でボードへ書き込み(アプリ領域のみ)
pio remote device monitor         # モニタ (115200)
```

**3. 代替1: uvx esportool で直書き(agentが使えない時 / 4MB ボード初回)**
- コンテナで `esptool merge-bin`(bootloader@0x0 + partitions@0x8000 + app@0x10000)で統合バイナリ作成
- ホストで `uvx esptool --chip <chip> --port /dev/cu.usbmodemXXX write-flash 0x0 merged-firmware.bin`
  (--baud は付けない。USB ブリッジ機種(Echo)で帯域エラーが出る)

**4. 代替2: RFC2217 経由** モニタ接続のリセット罠を回避したい場合:
- ホスト: `uvx --from esptool esp_rfc2217_server -v -p 4000 /dev/cu.usbmodemXXX`(常駐)
- コンテナ: `pio device monitor --port 'rfc2217://host.docker.internal:4000?ign_set_control'`

注意: ボード稼働中にモニタを頻繁に open すると DTR/RTS リセットの罠で ROM ダウンロードモード
(`rst:0x15 boot:0x20 ... waiting for download`)に落ちることがある。**USB 抜き差し(冷起動)直後に
モニタを開くのが最も安定**。落ちた場合は再書き込みで復帰。AtomS3 ボード定義には
`ARDUINO_USB_CDC_ON_BOOT` が無く、これ(platformio.ini で指定済み)がないとPC側シリアルにログが一切出ない。

### 設定
`include/secrets.h` に Wi-Fi(APリスト)、LINEトークン・ユーザーID、しきい値、(オプションで)
テレメトリ情報を書く(コミットされない):
```bash
cp include/secrets.h.example include/secrets.h  # 実値を書く
```

Wi-Fi は複数登録できる(自宅APメイン + 外テザリング等)。**接続中の AP は切り替えない**
(CSI と再キャリブレーションを安定させるため)。**切れたときだけ** 10秒後に再スキャンし、登録済み
AP のうち最も RSSI の強いものに切り替わる:
```cpp
#define WIFI_CREDENTIALS \
  { "home-ssid", "password" }, \
  { "home-ssid-2", "pass" }  // 外テザリング等の 2.4GHz 予備   // 2.4GHz。テザリングは「接続に使用するWi-Fi設定」を 2.4GHz にしてから
```

## ESPectre のキャリブレーション方法

- 検知器は**静かな(人がいない)部屋**での観測からしきい値を自動キャリブレーションする
- 初回起動後は周囲を動かさず待つ(実測約 27〜35 秒)。`[espectre] ready` / `ready=1` が出たら検知結果が信頼できる
- `ready` になるまでイベント通知は行わない
- 設置場所を変えたら再起動して再キャリブレーション(1台=1エリア)
- Wi-Fi power save は ESPectre が自動でオフにする
- **CSI トラフィック生成**: 既定は **WIFI_RAW**(Null Data + ACK。ICMP を通さない iPhone テザリングでも
  CSI が取れる)。**ESP32-C6 のみ DNS**(ESPectre 3.2.0 が C6 への WIFI_RAW を拒否するため)。
  `PHANTOM_TRAFFIC_PING`(ICMP 通す AP 向け)/`PHANTOM_TRAFFIC_DNS` を build_flags で選択可能

## LCD 表示仕様(AtomS3 128x64)

起動ステージと常時ステータスの2画面。現在状態をひと目でわかるように大きい文字(24x48px)で表示する。

### 起動ステージ(begin() 内の一時表示)

| 表示 | 段階 |
|---|---|
| `BOOT` / 大きい `0` | LCD 初期化後 |
| `WIFI` / 大きい `1` | Wi-Fi 接続中(最長20秒) |
| `RUN` / 大きい `2` | センサ起動完了 → 常時ステータスへ切替 |

### 常時ステータス(loop 中 250ms 間隔で更新)

| 下段 | 背景色 | 状態 |
|---|---|---|
| `CALIB` | 黒(黄文字) | キャリブレーション中。**動かさず待つ**(実測約30秒) |
| 大きい数字 `3s` | 黒 | **正常監視中**。最後の動き検知からの経過秒数 |
| `MOVE` | 緑 | 動きを検知した(緑の間は ESPectre が motion 状態。動きが止んでも**数秒残る**。緑が終わってから 0 再カウントし、その 10 秒時に疑似leave push) |
| `NO-WIFI` | 暗赤 | Wi-Fi 未接続(CSI が取れず検知できない。AP 側を確認) |
| 大きい数字 `12s` | 暗青 | 疑似 leave(10秒無動作)以降。数字はそのままカウントアップ |
| 大きい数字 `45s` | 紫 | 寝落下(30秒無動作)以降。カウントも LCD も続く(延長戦) |
| `WAIT` | 黒(黄文字) | キャリブレーション済みだがまだ一度も動きを検知していない |

- 上段に状態の文字(`LEAVE` / `SLEEP`)を追加表示する(通常時は `mv:` スコア)
- この秒数がそのまま疑似イベント判定の入力になる(10秒=疑似leave / 20秒=継続通知 / 30秒=寝落ち)
- 上段: `W:on R1 mv: 1234` = Wi-Fi接続 / キャリブレーション完了フラグ / 検知強度スコア

### LED 表示仕様(LCD 無しボード・RGB: Echo / 単色: C3=GPIO3, C6=GPIO15・アクティブLOW)

| 表示 | 状態 |
|---|---|
| RGB: 白(dim) / 単色: 常時点灯 | 起動中・正常監視中 |
| RGB: 青 / 単色: 1秒周期点滅 | **キャリブレーション中**(動かさない) |
| RGB: ピロリン音/緑 / 単色: 150ms点滅 | キャリブ完了後の動き検知中(Echoのみ音あり) |
| RGB: シアン/紫 / 単色: 消灯 | 疑似 leave(10秒〜)/ 寝落下(30秒〜)。静かな時間は LED も静かに |
| RGB: オレンジ / 単色: 150ms点滅 | Wi-Fi 未接続 |

### シリアルログの見方

```
[status] wifi=on ready=1 motion=1 movement=0.0 idle_ms=1283 count=1 up_ms=27491
[csi]    gen_pps=100 rx_pps=0 accepted_pps=91 admitted_pps=91 occ=0.91
[event]  motion (26208)
[line]   pushed motion    ← LINE push(テレメトリ有効時は [telemetry] も出る)
```

- `ready=1` になったら検知結果が信頼できる。`idle_ms` が疑似イベントの元データ
- `admitted_pps` が 0 に近づくと CSI が流れていない(Wi-Fi切断や AP 側の問題)。`occ` は検知ウィンドウの埋まり率

## BEACON(LINE Simple Beacon)の HWID 設定方法(Phase 2 用の下準備)

1. LINE Official Account Manager (https://manager.line.biz/beacon/register) で LINE Simple Beacon
   のビーコンを作成し **HWID(16進10文字/5バイト)を発行**する
2. `include/secrets.h` の `LINE_BEACON_HWID_HEX` に書く
3. Device Message は最大13バイト(16進26文字)。`LINE_BEACON_DM_HEX` で設定(未使用なら空文字 → `0x00` を送る)

送信される BLE Advertising の構成(公式仕様どおり:
`02 01 06` / `03 03 6F FE` / `[len] 16 6F FE 02 HWID(5B) 7F DM(0〜13B) 00…`

## LINE Messaging API の設定(サーバーレス構成)

- LINE Developers コンソール (https://developers.line.biz/) で Messaging API チャネルを作成し、
  **チャネルアクセストークン(長期)**を発行
- push 先のユーザーID(`U` で始まる)を確認(bot を友だち追加した自分)
- `include/secrets.h` に書く(**コミットされない**。漏れたらローテーション):
  ```cpp
  #define LINE_CHANNEL_TOKEN "..."
  #define LINE_USER_ID      "U..."
  ```
- ボードごとの表示ラベルは env の `PHANTOM_DEVICE_ID`(例: xiao-c6)、未指定なら `DEVICE_ID`
- ESP32 は `POST https://api.line.me/v2/bot/message/push` を TLS 証明書検証あり(公開CA
  `src/line_root_ca.h`、GlobalSign Root R46)で直接送る
- secrets から LINE 行を外しても安全: `[line] disabled` として push を行わない
- 無料プランは月間メッセージ枠が限られるため、**イベントは状態遷移時のみ + 同一種別の再送抑制**あり
  (`src/config.h`)

## 疑似イベントの判定仕様

| タイミング | 条件 | push |
|---|---|---|
| 動き検知 | ESPectre が動きを検知した(静止状態からの変化を含む) | 🏃「動きを検知しました(擬似)」 |
| 10秒無動作 | 最後の動作検知から 10 秒(`PSEUDO_LEAVE_MS`)検知なし | 🚪「動きがなくなりました(○秒...)」 |
| 20秒無動作 | 引き続き 20 秒(`PSEUDO_CONTINUED_MS`)検知なし | 🚪「継続して動きがありません(○秒...)」 |
| 30秒無動作 | 引き続き 30 秒(`SLEEP_SUSPECTED_MS`)検知なし | 😴「寝落ちの可能性(○秒...)」 |

- この4パターンのみ。**寝落下(30秒)以降は何も push しない**(カウントと表示は続き、色だけ変わる)
- push 文言には秒数が自動で付く(secrets.h のしきい値から生成)
- 表示も進動: LCD は 10秒〜暗青・30秒〜紫。LED は消灯(単色)/色変化(RGB)
- **sensing が ready の間だけイベントを評価する**(ready 往復しても push が連発しない)
- `motion==true` の継続中は keep-alive とし、長時間の動作でも擬似 leave を出さない
- 秒数計測の基準は **ESPectre の motion 状態が解除されてから**(LCD のカウントも同じ基準なので画面と push は一致)
- 実装: `lib/phantom_events/`(純C++)+ main.cpp 統合、テスト: `test/test_events/`(17/17)

## LINE push の通知内容(擬似イベント)

| type | push テキスト |
|---|---|
| `motion` | 🏃 動きを検知しました(擬似)(<端末ラベル>) |
| `pseudo_leave` | 🚪 動きがなくなりました(10秒無動作・擬似)(<端末ラベル>) |
| `pseudo_leave_continued` | 🚪 継続して動きがありません(20秒無動作・擬似)(<端末ラベル>) |
| `sleep_suspected` | 😴 寝落下の可能性(30秒無動作・擬似)(<端末ラベル>) |

同一 type は最小間隔(動き系10秒/静止系30秒・`config.h`)内で抑止する(無料プランの push 上限保護)。

これらは**完全に独自の疑似イベント**であり、LINE公式の `leave` イベントを偽装するものではない。

## 実機検証結果(2026-11-12 → 11-18)

| ボード | Wi-Fi | CSI | 動き検知→push | 表示 |
|---|---|---|---|---|
| AtomS3 | ✅(remote 書き込み込み) | ✅ WIFI_RAW occ 0.8-1.0 | ✅ 3段階 push 込み | ✅ LCD |
| ATOM Echo | ✅ | ✅ WIFI_RAW occ 0.9-1.0 | ✅ push到達(429込みで検証) | LED ✅ / **セビープ音未解決** |
| XIAO C3 | ✅(2AP切替込み) | ✅ WIFI_RAW occ 1.0 | ✅ push到達 | 単色LED ✅(点滅パターン実装) |
| XIAO C6 | ✅ | ✅ **DNS** occ 0.88-0.91 | ✅ push到達(2026-11-18) | 単色LED(実装済み) |

**既知の未解決**:
- Echo のビープ音: Class-D スピーカー(GPIO25・DAC駆動)。PWM→DAC方式に変更したが実機で音を出すまでに至らない
- XIAO ESP32-S3 Plus: PC に USB で列挙されない問題(ケーブル/基板の要因疑い)で検証が止まっている
- nRF Connect などでの LINE Simple Beacon 受信確認(Phase 2 実装後)

## 制約・既知の問題

- CSI は2.4GHz 802.11n でのみ取得可能(5GHz AP 環境では動作しない)
- ESPectre の動き検知は1台=1エリア。複数部屋には複数台が要る
- ESPectre ライブラリは GPLv3 か商用ライセンスのデュアルライセンス(`francescopace/ESPectre` 3.2.0)
- ESP32-C6 は ESPectre 3.2.0 で WIFI_RAW 不可(DNS トラフィックで回避済み)
- Wi-Fi CSI + BLE Advertising の無線共存は未検証(Phase 3 で最優先確認)

## Volume
- 共通: `pio-core`(ツールチェーン等), `pio-claude-config`
- 専用: ソース(バインドマウント `.:/workspace`、`.pio/` を含む)
