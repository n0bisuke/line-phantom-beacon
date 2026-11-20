# ハンズオン: XIAO ESP32-C6 で line-phantom-beacon を動かす(GitHub Codespaces / ローカル PC 対応)

XIAO ESP32-C6 は **LCD無し・ESP32-C6・4MBフラッシュ・オンボード単色LED(GPIO15)** です。
共通準備(LINE Bot・キー・ESPectre 解説)は [handson-junbi.md](handson-junbi.md) を先に済ませておいてください。
この文書だけで完結します。AtomS3 用は [handson-atoms3.md](handson-atoms3.md)、
ATOM Echo 用は [handson-atom-echo.md](handson-atom-echo.md)、XIAO C3 は [handson-xiao-esp32c3.md](handson-xiao-esp32c3.md)。

> 検証状態: **実機検証済み**(2026-11-18)。Wi-Fi 接続 / CSI(occ≈0.9)/ キャリブレーション完了を確認。
> **ESP32-C6 では ESPectre 3.2.0 の WIFI_RAW がライブラリ側で利用不可**のため、
> 本プロジェクトは C6 向けに **DNS クエリによるトラフィック生成**(`-DPHANTOM_TRAFFIC_DNS`)を
> 環境変数ではなく env の build_flags で設定済み(ビルドしたら自動で DNS モードになる)。

## 完成イメージ

1. C6 が Wi-Fi の CSI を解析して動きを検知(他ボードと同一ロジック。トラフィック生成だけ DNS)
2. 検知直後 → 🏃 push(文末ラベル **`(xiao-c6)`**)
3. 10秒無動作 → 🚪 push / 20秒 継続push / 30秒 😴 push(以降なし)
4. 表示は**シリアルログ + オンボードLEDの点滅**(単色・アクティブLOW)

## 前提

| 用意するもの | 備考 |
|---|---|
| XIAO ESP32-C6 | USB-C データ通信対応ケーブル。ポートは `cu.usbmodem…` |
| Wi-Fi(2.4GHz) | スマホのテザリング可。5GHz は CSI 不可 |
| LINE Developers アカウント | Messaging API チャネル + 長期トークン + ユーザーID |

## 書き込み方式の選択(重要)

| 方法 | 初回の bootloader 更新 | 使うコマンド |
|---|---|---|
| **ローカル PlatformIO(推奨)** | **自動で全部焼かれる**(bootloader+パーティション+アプリ) | `pio run -t upload -e seeed_xiao_esp32c6` |
| Codespaces(pio remote) | 書かれない → 初回のみ統合バイナリ直書きが必要 | 下記 Step 2 を1回だけ |

- `pio remote run -t upload` は**アプリ領域(0x10000)のみ**書き込む(実測)。
  初回は C6 チップに古い bootloader が残っていると IROM≈1MB を越えるアプリをマップできず
  **ログ無しの起動ループ**(`rst:0x3` 連発)になる(C3 で実証済み。詳細は
  `logs/2026-11-18-c3-investigation.md`)
- 一度 bootloader+パーティションが入れば、以後は pio remote での更新も可能

## Step 1: 秘密情報 → ビルド(Codespaces でもローカルでも同じ)

**このSTEPでやること: LINE のアクセストークンとユーザーID、Wi-Fi 情報を secrets.h に入れ、ファームをビルドします。** LINE準備がまだなら共通準備ガイド [handson-junbi.md](handson-junbi.md) の STEP 1 を先に。

```bash
cp include/secrets.h.example include/secrets.h
code include/secrets.h    # Wi-Fi(2.4GHz)/ LINE_CHANNEL_TOKEN / LINE_USER_ID
pio run -e seeed_xiao_esp32c6    # C6 用ファーム(Flash 45.7%)
pio test -e native               # ロジックのユニットテスト(ボード不要)
```

> 💡 `code` は Codespaces(VS Code) の開き方。**ローカル参加者はいつも使っているエディタでOK**
> (nano / vim / GUI エディタ / GitHub Web 画面での編集など)。やることは同じです。

- LINE push には **`(xiao-c6)`** ラベルが付く
- LINE 無料プランは **月200通**(検証で消費し、超えると `429`。毎月1日リセット)


> 注: `platformio.ini` の `default_envs`(既定 = AtomS3)は**書き換え不要**です。
> コマンドに `-e <自分のenv>` を付ければ常にそのボードでビルド/書き込みされます。

## Step 2(Codespaces の場合のみ): 初回の統合バイナリ直書き

> 📌 **直書きの前に: ボードをホスト PC と USB 接続してください**(データ通信用ケーブル。
> 電源 LED が点灯していれば電力は来ている)。ポート名は `ls /dev/cu.*` で確認:
> - **USB 直結チップ(C3/C6/S3)= `cu.usbmodem…`、USB-UART ブリッジ(Echo)= `cu.usbserial-…`**
> - ポートが出ない場合はケーブル差し替え・USB-C 向き反転・別ポートを試す


**このSTEPでやること: Codespaces ルート限定の初回手順です。** bootlaoder+パーティション込の統合バイナリをホスト PC から焼きます。ローカルの `pio run -t upload` では不要(全部まとめて焼かれるため)。

1. コンテナ内で統合バイナリ作成:

   ```bash
   esptool --chip esp32c6 merge-bin -o .pio/build/seeed_xiao_esp32c6/merged-firmware.bin \
     --flash_mode dio --flash_size 4MB \
     0x0     .pio/build/seeed_xiao_esp32c6/bootloader.bin \
     0x8000  .pio/build/seeed_xiao_esp32c6/partitions.bin \
     0x10000 .pio/build/seeed_xiao_esp32c6/firmware.bin
   ```

2. ホスト PC で直書き(USB 接続後):

   ```bash
   cd <cloneしたリポジトリ>
   uvx esptool --chip esp32c6 --port /dev/cu.usbmodemXXXX write-flash 0x0 \
     .pio/build/seeed_xiao_esp32c6/merged-firmware.bin
   ```


   > 💡 **Codespaces でビルドした場合**: `.pio/build/**` は Codespace 上にしかありません。
   > ホストへは `gh codespace cp -r <codespace名>:workspace/.pio/build/seeed_xiao_esp32c6/merged-firmware.bin .`
   > で取得してから焼きます。**ローカルで `pio run` した場合、`pio run -t upload` が全部焼くのでこの手順は不要**。

   完了すると `Hash of data verified.` → 自動リセット → ファーム起動。
   **一度焼けば以後は pio remote の更新で良い**

### ローカル PlatformIO での書き込み(一番シンプル・推奨)

ホスト PC に PlatformIO を入れていれば、**転送も統合バイナリも不要**でこの 1 コマンドです
(bootloader+パーティション+アプリが全領域焼かれ、4MB ボードの初回手順も自動で済みます):

```bash
pip install -U platformio     # ホストに未インストールの場合のみ(初回ビルドはツールチェーン約3GBで10分+)
pio run -t upload -e seeed_xiao_esp32c6    # ボードを USB 接続した状態で
pio device monitor            # シリアルモニタ(Ctrl+C で終了)
```

Codespaces でビルドした成果物を使う場合(転送あり)は上の Step 2 の手順を参照。
## Step 3: 実機確認(シリアルログ + LED)

**このSTEPでやること: ボードを設置して、シリアルログ・LED の点滅・LINE 通知を確認します。**

```
[wifi] 2 APs registered, scanning...
[wifi] connected channel=8 rssi=-38
[espectre] traffic=dns (DNS クエリ生成。C6 や ICMP 制限環境向け)
[csi] gen_pps=101 rx_pps=0 accepted_pps=100 admitted_pps=91 occ=0.91   ← DNS でも CSI は流れる
[espectre] ready (キャリブレーション完了)
[event] motion → [line] direct push                                     ← 手を振ると push
```

### LED の点滅パターン(GPIO15・アクティブLOW)

| LED | 状態 |
|---|---|
| 常時点灯 | 起動中 / 正常監視中 |
| 1秒周期点滅 | キャリブレーション中(約30秒。動かさない) |
| 150ms周期点滅 | 動き検知中 / Wi-Fi 未接続 |
| 消灯 | 疑似 leave / 寝落下(静寂) |

## トラブルシュート(C6 固有)

| 症状 | 対処 |
|---|---|
| `[espectre] fault: invalid traffic generator mode` | **WIFI_RAW でビルドしている古いファーム**。`pio run -e seeed_xiao_esp32c6` で再ビルド(C6 は DNS モードが選択される)して焼き直す |
| `rst:0x3` を 0.2〜0.3秒周期で連発・ログ無し | bootloader 未更新(Step 2)。C3 での調査記録 `logs/2026-11-18-c3-investigation.md` 参照 |
| モニタ open のたびにダウンロードモードに落ちる | USB 再挿し → 冷起動直後にモニタ。もしくはトンネル(`esp_rfc2217_server`)経由で解報 |
| テレメトリ(Grafana)が無効 | これは仕様(オプション機能)。有効化は README の「 Grafana Cloud へのテレメトリ送信」参照 |

## ボード差メモ

- 電波: Wi-Fi 6(Zigbee付き)。CSI 取得は DNS トラフィックで安定(occ≈0.9 前半を確認)
- 初期 bootloader 更新の必要性は C3/Echo と同じ(4MB ボード共通)
- ESPectre の DNS トラフィックは ICMP 面制限(テザリング)でも動く — WIFI_RAW が使えた環境での C3/Echo との
  差分は「CSI 供給の方法」だけで、動き検知のロジックは全ボード同一
