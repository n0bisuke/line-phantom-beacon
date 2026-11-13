# line-phantom-beacon

M5Stack AtomS3(ESP32-S3)1台で、Wi-Fi CSI による人の動き検知(ESPectre)と LINE Simple Beacon 形式の BLE Advertising を行い、独自の疑似 `leave` / 寝落ち疑いイベントを LINE Messaging APIへ通知する実験プロジェクト。

⚠️ 本プロジェクトが生成する `pseudo_leave` / `sleep_suspected` は**完全に独自の疑似イベント**であり、LINE公式の `leave` イベントや LINEプラットフォームを偽装するものではありません。また、CSIによる動き検知は個人を識別せず「動きがない=退室」「動きがない=睡眠」も一切断定できません。

## システム構成

```
[AtomS3] ==Wi-Fi CSI==> ESPectre 動き検知 ──状態遷移──┐
[AtomS3] --HTTPS(疑似イベント直接push)────────────────┘--> [LINE Messaging API] --> ユーザー
[AtomS3] --BLE adv (LINE Simple Beacon 形式, Phase 2)--> [スマホ/サーバ] (未実装)
```

- AtomS3 が ESPectre の動き検知履歴から擬似イベントを生成し、**LINE Messaging API へ直接 HTTPS push する(サーバーレス構成)**
- チャネルアクセストークンは ESP32 の `include/secrets.h` に置き、**Git にはコミットしない**。漏れたら LINE Developers コンソールでローテーション
- 同一イベント種別の再送抑制を ESP32 側で行う(LINE 無料メッセージ枠の保護)

フェーズ管理:

| Phase | 内容 | 状態 |
|---|---|---|
| 1 | ESPectre 単独動作(CSI 動き検知) | **コード・実機検証とも完了**(wifi_raw 必須、キャリブレーション約27秒) |
| 2 | LINE Simple Beacon BLE 発信 | フレーム生成は実装・テスト済。デバイスからのBLE送信は未実装 |
| 3 | Wi-Fi CSI + BLE 同時動作 | 未着手(最優先の技術リスク) |
| 4 | 疑似イベント状態遷移 | **ロジック実装・テスト済 + main.cpp に統合済**、実機検証は未確認 |
| 5 | LINE Messaging API push | **ESP32 直接 push(サーバーレス)としてコード完成**、実機検証は未確認(LINE トークン設定待ち) |

## 必要なハードウェア

- M5Stack AtomS3(`m5stack-atoms3`)
- **2.4GHz の Wi-Fi 4 (802.11n) アクセスポイント**(5GHz専用では CSI が取れない)
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
pio run                              # ビルド。初回はツールチェーンを数GBダウンロードするので10分以上かかる
```

### ネイティブテスト(ボード不要。コンテナ内で完結)
```bash
pio test -e native                   # lib/phantom_events + lib/line_beacon_frame のテスト
```

ビルド成果物は `.pio/build/m5stack-atoms3/`(ホストからも見える。Git管理外)。

## ESP32への書き込み・シリアルモニタ(macOS + OrbStack 実績あり)

標準は **pio remote**(コンテナからコマンド1つ。ホストに3GBのツールチェーンは不要)。

**1. 初回だけ(ホストmacOS側で)**
```bash
pio account login          # PlatformIO アカウント(対話ログイン)
pio remote agent start     # 常駐させる(ターミナルを閉じない)
```

**2. トークンを `.env` に追加してからコンテナで実行**
```
PLATFORMIO_AUTH_TOKEN=<platformio.org/account/tokens で発行したトークン>
```
```bash
# コンテナ内
export PLATFORMIO_AUTH_TOKEN=$(sed -n 's/^PLATFORMIO_AUTH_TOKEN=//p' .env)
pio remote run -t upload          # ビルド(コンテナ) → クラウド経由でボードへ書き込み
pio remote device monitor         # モニタ (115200)
```

**3. 代替1: uvx esptool で直書き(Remote/agentが使えないとき)**
- コンテナで `esptool --chip esp32s3 merge_bin -o merged-firmware.bin ...`(bootloader@0x0 + partitions@0x8000 + boot_app0.bin@0xe000 + firmware.bin@0x10000)を作る
- ホストで `uvx esptool --chip esp32s3 --port /dev/cu.usbmodemXXX -b 460800 write-flash 0x0 .pio/build/m5stack-atoms3/merged-firmware.bin`
- モニタ: `uv run --with pyserial python -m serial.tools.miniterm /dev/cu.usbmodemXXX 115200`

**4. 代替2: RFC2217経由**(platformio.ini のコメント参照、未検証)

注意: OrbStackの `orb usb attach` は macOS からデバイスを奪う。`orb usb detach 00100000` + USB抜き差しで戻る。AtomS3ボード定義には `ARDUINO_USB_CDC_ON_BOOT` が無く、これ(build_flags で指定済み)が無いとPC側のシリアルにログが一切出ない。

### 設定
`include/secrets.h` に Wi-Fi(APリスト)、HWID、Device Message、LINEトークン・ユーザーID、しきい値を書く(コミットされない):
```bash
cp include/secrets.h.example include/secrets.h  # 実値を書く
```

Wi-Fi は複数登録できる(自宅APメイン + 外テザリング等)。**接続中の AP は切り替えない**(CSI と再キャリブレーションを安定させるため)。**切れたときだけ** 10秒後に再スキャンし、登録済み AP のうち最も RSSI の強いものに切り替わる:
```cpp
#define WIFI_CREDENTIALS \
  { "home-ssid", "password" }, \
  { "", "" }  // 予備(空行はスキップ)
```
- CSI は 2.4GHz 802.11n でしか取れないため、5GHz の AP(テザリング含む)は 2.4GHz 向けの接続設定にしてから使う
- AP 切り替え時は sensing が一度停止し、再接続後に自動で再キャリブレーション(再び ready=1 まで待つ)

## ESPectre のキャリブレーション方法(公式 SDK.md の仕様に基づく)

- Lightweight 検知器は**静かな(静的な)部屋**での観測からしきい値を自動キャリブレーションする
- 初回起動後は周囲を動かさず待つ。`[status] ... ready=1` がログに出たらキャリブレーション完了=検知結果が信頼できる
- `ready` になるまでのイベント通知は行わない(ライブラリ仕様: `ready_to_publish` が false の間は非通知)
- キャリブレーションが失敗しても致命的ではない(現行のしきい値で検知を継続する)
- 設置場所を変えたら再起動して再キャリブレーションさせる(1台で1エリアのみをカバーする)
- 既定値: `csi_target_pps=100`(CSIの目標パケットレート)、しきい値はキャリブレーションで決まるため NVS に保存されない
- Wi-Fi power save は ESPectre が自動でオフにする(`WiFi.setSleep(true)` を呼ばないこと)
- CSI パケットの供給源(トラフィック生成)は既定の PING(ICMP)ではなく **WIFI_RAW**(Null Data フレーム + ACK)を使用。ICMP を通さない AP(iPhone ホットスポット等)でも CSI が取れる(`src/motion_sensor.cpp` で設定)

## LCD 表示仕様(AtomS3 128x64)

起動ステージと常時ステータスの2画面。現在状態をひと目でわかるように大きい文字(24x48px)で表示する。

### 起動ステージ(begin() 内の一時表示)

| 表示 | 段階 |
|---|---|
| `BOOT` / 大きい `0` | LCD 初期化後 |
| `WIFI` / 大きい `1` | Wi-Fi 接続中(最長20秒) |
| `RUN` / 大きい `2` | センサ起動完了 → 常時ステータスへ切替 |

### 常時ステータス(loop 中 250ms 間隔で更新)

下段(大きい文字)が状態、上段(小さい文字)が補助情報:

| 下段 | 背景色 | 状態 |
|---|---|---|
| `CALIB` | 黒(黄文字) | キャリブレーション中。**動かさず待つ**(実測約27秒) |
| 大きい数字 `3s` | 黒 | **正常監視中**。最後の動き検知からの経過秒数 |
| `MOVE` | 緑 | 動きを検知した(緑の間は ESPectre が motion 状態。動きが止まっても**数秒間は緑のまま**。緑が終わってから数字が 0 から再カウントされ、その 10 秒時に疑似leave push) |
| `NO-WIFI` | 暗赤 | Wi-Fi 未接続(CSI が取れず検知できない。AP 側を確認) |
| 大きい数字 `12s` | 暗青 | 疑似 leave(10秒無動作)以降。数字はそのままカウントアップし続ける |
| 大きい数字 `45s` | 紫 | 寝落ち(30秒無動作)以降。カウントも LCD も続く(延長戦) |
| `WAIT` | 黒(黄文字) | キャリブレーション済みだがまだ一度も動きを検知していない |

- 上段に状態の文字(`LEAVE` / `SLEEP`)を追加表示する(通常時は `mv:` スコア)
- この秒数がそのまま疑似イベント判定の入力になる(10秒=疑似leave / 20秒=継続通知 / 30秒=寝落ち)
- 上段: `W:on R1 mv: 1234` = Wi-Fi接続 / キャリブレーション完了フラグ / 検知強度スコア
- 対応するシリアルログ: `[status]`(5秒間隔)、`[csi]`(CSI パケットレート診断)、`[event] motion`(検知の都度)

### シリアルログの見方

```
[status] wifi=on ready=1 motion=1 movement=0.0 idle_ms=1283 count=1 up_ms=27491
[csi]    gen_pps=100 rx_pps=0 accepted_pps=91 admitted_pps=91 occ=0.91
[event]  motion (26208)
```

- `ready=1` になったら検知結果が信頼できる。`idle_ms` が疑似イベントの元データ
- `admitted_pps` が 0 に近づくと CSI が流れていない(Wi-Fi切断や AP 側の問題)。`occ` は検知ウィンドウの埋まり率
- 既知の表示課題: `movement` が常に 0.0 で表示される(検知自体は動作。表示用フィールドの見直し予定)

## LINE Beacon の HWID 設定方法

1. LINE Official Account Manager (https://manager.line.biz/beacon/register) で LINE Simple Beacon のビーコンを作成し **HWID(16進10文字/5バイト)を発行**する
2. `include/secrets.h` の `LINE_BEACON_HWID_HEX` に書く
3. Device Message は最大13バイト(16進26文字)。`LINE_BEACON_DM_HEX` で設定(未使用なら空文字 → `0x00` を送る)

送信される BLE Advertising の構成(公式仕様どおり):
`02 01 06` / `03 03 6F FE` / `[len] 16 6F FE 02 HWID(5B) 7F DM(0〜13B) 00…`

## LINE Messaging API の設定(Phase 5・サーバーレス構成)

- LINE Developers コンソール (https://developers.line.biz/) で Messaging API チャネルを作成し、**チャネルアクセストークン(長期)**を発行
- push 先のユーザーID(`U` で始まる)を確認(bot を友だち追加した自分)
- `include/secrets.h` に書く(**コミットされない**。漏れたらローテーション):
  ```cpp
  #define LINE_CHANNEL_TOKEN "..."
  #define LINE_USER_ID      "U..."
  ```
- ESP32 は `POST https://api.line.me/v2/bot/message/push` を TLS 証明書検証あり(公開CA `src/line_root_ca.h`、GlobalSign Root R46 / 有効期限 2029-03-18 まで、切れたら再取得)で直接送る
- 無料プランは月間メッセージ枠が限られるため、**イベントは状態遷移時のみ + 同一種別の再送抑制**あり(`src/config.h` の `kLineMotionIntervalMs` / `kLineQuietIntervalMs`)
- 注意(設計変更): 当初案の「Node.js 中継サーバー経由(トークンはESP32に置かない)」から、**トークンをESP32に置く直接push構成**に変更した。リポジトリ運用とトークンローテーションを厳守すること

## 疑似イベントの判定仕様(Phase 4)

| タイミング | 条件 | push |
|---|---|---|
| 動き検知 | ESPectre が動きを検知した(静止状態からの変化を含む。退室判定後の再開も同様) | 🏃「動きを検知しました(擬似)」 |
| 10秒無動作 | 最後の動作検知から 10 秒(`PSEUDO_LEAVE_MS`)検知なし | 🚪「動きがなくなりました(○秒...)」 |
| 20秒無動作 | 引き続き 20 秒(`PSEUDO_CONTINUED_MS`)検知なし | 🚪「継続して動きがありません(○秒...)」 |
| 30秒無動作 | 引き続き 30 秒(`SLEEP_SUSPECTED_MS`)検知なし | 😴「寝落ちの可能性(○秒...)」 |

- この4パターンのみ。**寝落ち(30秒)以降は何も push しない**(カウントと LCD 表示は続き、色だけ変わる)
- push 文言には秒数が自動で付く(secrets.h のしきい値から生成。しきい値を変えれば文言も追従)
- LCD も連動: 10秒〜は背景が暗青、30秒〜は紫(下記「LCD 表示仕様」参照)
- **sensing が ready(キャリブレーション完了)の間だけイベントを評価する**。ready が落ちた期間はノイズ扱いで何もpushせず、復帰時に状態機を初期化(reset)して静観を再開する → CSI品質が乱れて ready が往復しても push が連発しない
- 閾値は `include/secrets.h` の `PSEUDO_LEAVE_MS` / `PSEUDO_CONTINUED_MS` / `SLEEP_SUSPECTED_MS` で変更可能
- `motion==true` の継続中は keep-alive とし、長時間の動作でも擬似 leave を出さない
- 秒数計測の基準は **ESPectre の motion 状態が解除されてから**(物理的な動きが止まっても motion は数秒保持されるため、動き停止→push の実間隔は 10秒 + 数秒になる。LCD のカウントも同じ基準なので画面と push は一致する)
- 起動直後に検知実績がない静止状態では空室通知をしない
- 実装: `lib/phantom_events/`(純C++)+ main.cpp 統合、テスト: `test/test_events/`(3段階シーケンス含む10テスト)

## LINE push の通知内容(擬似イベント)

例(`PSEUDO_LEAVE_MS=10000` の場合):

| type | push テキスト |
|---|---|
| `motion` | 🏃 動きを検知しました(擬似)(atoms3-desk) |
| `pseudo_leave` | 🚪 動きがなくなりました(10秒無動作・擬似)(atoms3-desk) |
| `pseudo_leave_continued` | 🚪 継続して動きがありません(20秒無動作・擬似)(atoms3-desk) |
| `sleep_suspected` | 😴 寝落ちの可能性(30秒無動作・擬似)(atoms3-desk) |

同一 type は最小間隔(動き系10秒/静止系30秒・`config.h`)内で抑止する(無料プランの push 上限保護)。

これらは**完全に独自の疑似イベント**であり、LINE公式の `leave` イベントを偽装するものではない。「動きがない=退室」「動きがない=睡眠」は断定しない(通知文にも「擬似」を残している)。

## 実機検証結果

**確認済み(2026-11-12)**:
- 書き込み: `pio remote run -t upload` 成功(コンテナでビルド → ホストの AtomS3 へ)
- AtomS3 のファーム起動・LCD 表示(起動ステージ / 大きいステータス表示)
- **ESPectre キャリブレーション: 起動約27秒で `ready=1`**
- **動き検知: `[event] motion` を実機で確認(idle秒数のカウント、motion on/off 遷移も正常)**
- **CSI 取得: iPhoneホットスポットは ICMP を通さないため 既定の ping では CSI が流れない。`traffic_generator_mode = WIFI_RAW`(Null Data + ACK)に変更でCSI 82〜96 packets/sec を取得**
- シリアルモニタは **コールドブート(USB抜き差し)直後に開く** と安全。ボード稼働中に頻繁に開閉すると DTR/RTS リセットの罠で ROM ダウンロードモード(`rst:0x15 boot:0x20 ... waiting for download`)に落ちることがある(抜き差しで復帰)

**未確認**:
- 動き検知の感度・検知距離の調整(部屋・設置位置依存)
- nRF Connect などでの LINE Simple Beacon フレーム受信確認(Phase 2 実装後)
- CSI と BLE の無線共存(Phase 3)
- ESP32 からの LINE 直接 push(トークン設定待ち)+ 再送抑制の実際の動作

## 制約・既知の問題

- コンテナから USB シリアルへアクセスできないため、書き込み・モニタはホスト側で行う
- ESPectre ライブラリは GPLv3 か商用ライセンスのデュアルライセンス(`francescopace/ESPectre` 3.2.0)
- CSI は2.4GHz 802.11n でのみ取得可能(5GHz AP 環境では動作しない)
- ESPectre の動き検知は1台=1エリア。複数部屋には複数台が要る
- Wi-Fi CSI + BLE Advertising の同時動作(ESP32-S3 の無線共存)は一般的に可能とされるが、本設定での実績は未検証(Phase 3 で最優先確認)

## Volume
- 共通: `pio-core`(ツールチェーン等), `pio-claude-config`
- 専用: ソース(バインドマウント `.:/workspace`、`.pio/` を含む)
