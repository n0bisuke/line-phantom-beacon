# line-phantom-beacon

M5Stack AtomS3(ESP32-S3)1台で、Wi-Fi CSI による人の動き検知(ESPectre)と LINE Simple Beacon 形式の BLE Advertising を行い、独自の疑似 `leave` / 寝落ち疑いイベントを LINE Messaging APIへ通知する実験プロジェクト。

⚠️ 本プロジェクトが生成する `pseudo_leave` / `sleep_suspected` は**完全に独自の疑似イベント**であり、LINE公式の `leave` イベントや LINEプラットフォームを偽装するものではありません。また、CSIによる動き検知は個人を識別せず「動きがない=退室」「動きがない=睡眠」も一切断定できません。

## システム構成

```
[スマホ] --BLE enter--> [LINEサーバ] --Webhook--> [Node.js 中継]
[AtomS3] --BLE adv (LINE Simple Beacon 形式)------------↑
[AtomS3] --HTTP (独自疑似イベント JSON)------------------↑
[AtomS3] ==Wi-Fi CSI==> ESPectre 動き検知 ──状態遷移──┘
[Node.js] ----HTTPS push----> [LINE Official Account] --> ユーザー
```

- `enter` は本物の LINE Beacon Webhook(hwid でビーコンを特定。dm は本プロジェクトでは状態通知には使わない)
- `pseudo_leave` / `sleep_suspected` / `motion_resumed` は AtomS3 自身が ESPectre の動き検知履歴から生成し、独自のイベント形式(HTTP POST)で中継サーバーへ送る
- 中継サーバーがユーザー紐付け・署名検証・重複抑制を担い、許可済みユーザーへ push する
- ESP32 側に LINE のチャネルアクセストークンは持たない

フェーズ管理:

| Phase | 内容 | 状態 |
|---|---|---|
| 1 | ESPectre 単独動作(CSI 動き検知) | コード完了、実機検証は**未確認** |
| 2 | LINE Simple Beacon BLE 発信 | フレーム生成は実装済(テスト済見込み)、BLE送信は未実装 |
| 3 | Wi-Fi CSI + BLE 同時動作 | 未着手(最優先の技術リスク) |
| 4 | 疑似イベント状態遷移 | 純C++ロジック実装済、統合は未着手 |
| 5 | Node.js 中継 + LINE Messaging API | 未着手 |

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
`include/secrets.h` に Wi-Fi SSID/パスワード、HWID、Device Message、中継URL、しきい値を書く(コミットされない):
```bash
cp include/secrets.h.example include/secrets.h  # 実値を書く
```

## ESPectre のキャリブレーション方法(公式 SDK.md の仕様に基づく)

- Lightweight 検知器は**静かな(静的な)部屋**での観測からしきい値を自動キャリブレーションする
- 初回起動後は周囲を動かさず待つ。`[status] ... ready=1` がログに出たらキャリブレーション完了=検知結果が信頼できる
- `ready` になるまでのイベント通知は行わない(ライブラリ仕様: `ready_to_publish` が false の間は非通知)
- キャリブレーションが失敗しても致命的ではない(現行のしきい値で検知を継続する)
- 設置場所を変えたら再起動して再キャリブレーションさせる(1台で1エリアのみをカバーする)
- 既定値: `csi_target_pps=100`(CSIの目標パケットレート)、しきい値はキャリブレーションで決まるため NVS に保存されない
- Wi-Fi power save は ESPectre が自動でオフにする(`WiFi.setSleep(true)` を呼ばないこと)

## LINE Beacon の HWID 設定方法

1. LINE Official Account Manager (https://manager.line.biz/beacon/register) で LINE Simple Beacon のビーコンを作成し **HWID(16進10文字/5バイト)を発行**する
2. `include/secrets.h` の `LINE_BEACON_HWID_HEX` に書く
3. Device Message は最大13バイト(16進26文字)。`LINE_BEACON_DM_HEX` で設定(未使用なら空文字 → `0x00` を送る)

送信される BLE Advertising の構成(公式仕様どおり):
`02 01 06` / `03 03 6F FE` / `[len] 16 6F FE 02 HWID(5B) 7F DM(0〜13B) 00…`

## LINE Messaging API の設定(Phase 5 実装時)

- LINE Developers コンソールでチャネルを作成し、チャネルシークレット / チャネルアクセストークンを発行
- **トークン・シークレットは `.env` に置き Gitにコミットしない**(中継サーバー側のみで保持)
- Beacon Webhook を有効化し、ユーザーは bot を友だち追加して `enter` を受信用に許可する
- 通知先ユーザーの登録・管理は中継サーバー側の設定で行う

## 疑似イベントの判定仕様(Phase 4)

| イベント | 条件 | 実装 |
|---|---|---|
| `motion` | ESPectre が動きを検知(静止状態から) | 状態遷移時のみ |
| `pseudo_leave` | 最後の動作検知から 30 秒間(初期値)検知なし | しきい値到達時に1回だけ |
| `sleep_suspected` | 最後の動作検知から 3 分間(初期値)検知なし | しきい値到達時に1回だけ |
| `motion_resumed` | 静止判定(pseudo_leave / sleep_suspected 後)に再び動きを検知 | 遷移時 |

- 30秒/3分は `include/secrets.h` の `PSEUDO_LEAVE_MS` / `SLEEP_SUSPECTED_MS` で変更可能
- `motion==true` の継続中は keep-alive とし、長時間の動作でも擬似 leave を出さない
- 起動直後に検知実績がない静止状態では空室通知をしない
- 実装: `lib/phantom_events/`(純C++)、テスト: `test/events/`

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
- Node.js 中継・LINE連携全体(Phase 5)

## 制約・既知の問題

- コンテナから USB シリアルへアクセスできないため、書き込み・モニタはホスト側で行う
- ESPectre ライブラリは GPLv3 か商用ライセンスのデュアルライセンス(`francescopace/ESPectre` 3.2.0)
- CSI は2.4GHz 802.11n でのみ取得可能(5GHz AP 環境では動作しない)
- ESPectre の動き検知は1台=1エリア。複数部屋には複数台が要る
- Wi-Fi CSI + BLE Advertising の同時動作(ESP32-S3 の無線共存)は一般的に可能とされるが、本設定での実績は未検証(Phase 3 で最優先確認)

## Volume
- 共通: `pio-core`(ツールチェーン等), `pio-claude-config`
- 専用: ソース(バインドマウント `.:/workspace`、`.pio/` を含む)
