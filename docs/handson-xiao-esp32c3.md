# ハンズオン: XIAO ESP32-C3 で line-phantom-beacon を動かす(GitHub Codespaces / ローカル PC 対応)

XIAO ESP32-C3 は **LCD無し・ESP32-C3(rev v0.3 相当の小さいボード)** です。
共通準備(LINE Bot・キー・ESPectre 解説)は [handson-junbi.md](handson-junbi.md) を先に済ませておいてください。
この文書だけで完結します。AtomS3 用は [handson-atoms3.md](handson-atoms3.md)、
ATOM Echo 用は [handson-atom-echo.md](handson-atom-echo.md)。

> 検証状態: **実機検証済み**(2026-11-18, チップ v0.3 / 4MB フラッシュ)。
> Wi-Fi 接続(2AP切り替え)/ CSI(occ≈1.0)/ 動き検知 / LINE push まで確認。
> **ただし初回の書き込み手順が重要です**(下記 Step 2 を必ず通すこと)。

## 完成イメージ

1. C3 が Wi-Fi の CSI を解析して動きを検知(他ボードと同一ロジック)
2. 検知直後 → 🏃 push(文末ラベル **`(xiao-c3)`**)
3. 10秒無動作 → 🚪 push / 20秒 継続push / 30秒 😴 push(以降なし)
4. 表示は**シリアルログ + オンボードLEDの点滅**(LCD/RGB なし。LED は単色・アクティブLOW)

## オンボード LED の点滅パターン(GPIO3/D6)

多色 LED がないため、**単色 LED の点滅パターン**で状態を表します:

| LED | 状態 |
|---|---|
| 常時点灯 | 起動中 / 正常監視中 |
| 1秒周期の点滅 | **キャリブレーション中**(約30秒。動かさない) |
| 150ms周期の点滅 | 動き検知中 / Wi-Fi 未接続 |
| 消灯 | 疑似 leave(10秒〜)/ 寝落下(30秒〜)。静かな時間は LED も静かに |


## 前提

| 用意するもの | 備考 |
|---|---|
| XIAO ESP32-C3 | USB-C **データ通信対応**ケーブル。ポートは `cu.usbmodem…` に出る |
| Wi-Fi(2.4GHz) | スマホのテザリング可。5GHz は CSI 不可 |
| LINE Developers アカウント | Messaging API チャネル + 長期トークン + ユーザーID |
| Host PC | USB の繋がる PC(uv / esptool で初回直書きするため必須) |

## ⚠️このボード特有の注意: 「初回は bootloader+パーティションの書き換え」

`pio remote run -t upload` は **アプリ領域(0x10000)のみ**を書き込み、bootloader(0x0)と
パーティション(0x8000)は書き換えません。そのため:

- **そのボードに過去に別プロジェクトのファームを焼いたことがある場合**、チップには古い
  bootloader が残っています。古い bootloader は IROM≈1MB を越えるアプリ(本プロジェクトは
  約1.4MB)を正しくマップできず、**ログ無しの起動ループ**(`rst:0x3` を ~0.25s 周期で連発)になります
- 対処: **初回のみ「統合バイナリ」で bootloader+パーティション+アプリを丸ごと焼く**(Step 2)。
  一度焼けば以後は `pio remote run -t upload` だけで更新できます(Step 3)
- 実測(2026-11-18): 同一ビルドで「bootloader 更新のみで起動ループ→正常起動に変化」を確認

## Step 1: 秘密情報 → ビルド(Codespaces でもローカルでも同じ)

**このSTEPでやること: LINE のアクセストークンとユーザーID、Wi-Fi 情報を secrets.h に入れ、ファームをビルドします。** LINE準備がまだなら共通準備ガイド [handson-junbi.md](handson-junbi.md) の STEP 1 を先に。

```bash
cp include/secrets.h.example include/secrets.h
code include/secrets.h    # Wi-Fi(2.4GHz)/ LINE_TOKEN / LINE_USER_ID
pio run -e seeed_xiao_esp32c3    # C3 用ファーム(Flash 44.6%)
pio test -e native               # ロジックのユニットテスト(ボード不要)
```

> 💡 `code` は Codespaces(VS Code) の開き方。**ローカル参加者はいつも使っているエディタでOK**
> (nano / vim / GUI エディタ / GitHub Web 画面での編集など)。やることは同じです。

- LINE push には **`(xiao-c3)`** ラベルが付く(C3 env の `PHANTOM_DEVICE_ID=xiao-c3`)
- LINE 無料プランは **月200通**。検証で消費し、超えると `429`(毎月1日リセット)


> 注: `platformio.ini` の `default_envs`(既定 = AtomS3)は**書き換え不要**です。
> コマンドに `-e <自分のenv>` を付ければ常にそのボードでビルド/書き込みされます。

## Step 2: 初回のみ「統合バイナリ」をホストから直書き

> 📌 **直書きの前に: ボードをホスト PC と USB 接続してください**(データ通信用ケーブル。
> 電源 LED が点灯していれば電力は来ている)。ポート名は `ls /dev/cu.*` で確認:
> - **USB 直結チップ(C3/C6/S3)= `cu.usbmodem…`、USB-UART ブリッジ(Echo)= `cu.usbserial-…`**
> - ポートが出ない場合はケーブル差し替え・USB-C 向き反転・別ポートを試す


**このSTEPでやること: 初回書き込みのため、bootloader+パーティション込の統合バイナリをホスト PC から焼きます。** 一度だけ実施すれば、以後は普通の更新手順で済みます。

1. Codespaces/コンテナ内で統合バイナリを作成(bootloader+partitions+app):

   ```bash
   esptool --chip esp32c3 merge-bin -o .pio/build/seeed_xiao_esp32c3/merged-firmware.bin \
     --flash_mode dio --flash_size 4MB \
     0x0     .pio/build/seeed_xiao_esp32c3/bootloader.bin \
     0x8000  .pio/build/seeed_xiao_esp32c3/partitions.bin \
     0x10000 .pio/build/seeed_xiao_esp32c3/firmware.bin
   ```

2. ホストPC(C3 を USB 接続)で直書き:

   ```bash
   cd <cloneしたリポジトリ>
   uvx esptool --chip esp32c3 --port /dev/cu.usbmodem101 write-flash 0x0 \
     .pio/build/seeed_xiao_esp32c3/merged-firmware.bin
   ```


   > 💡 **Codespaces でビルドした場合**: `.pio/build/**` は Codespace 上にしかありません。
   > ホストへは `gh codespace cp -r <codespace名>:workspace/.pio/build/seeed_xiao_esp32c3/merged-firmware.bin .`
   > で取得してから焼きます(gh は `brew install gh && gh auth login`)。
   > **ローカルで `pio run` できた参加者はこの手順は不要**(`pio run -t upload` が全部焼きます)。

   - `--baud` は指定しない(既定115200が確実。約2分)
   - 完了すると `Hash of data verified.` → 自動リセット → ファーム起動

## Step 3: 通常運用(更新はアプリ領域だけ)

**このSTEPでやること: 2回目以降のファーム更新手順(アプリ領域だけの焼き直しで済みます)。**

- **Codespaces(pio remote)**: ホストで `pio remote agent start` →
  `pio remote run -t upload -e seeed_xiao_esp32c3` → `pio remote device monitor`
- **ローカル完結**: `pio run -t upload -e seeed_xiao_esp32c3` → `pio device monitor`
- 初回以降はパーティション変更がなければ直接書き込み不要

## Step 4: 実機で確認(シリアルログのみ)

**このSTEPでやること: ボードを設置して、シリアルログと LINE 通知を確認します。**

```
[wifi] 2 APs registered, scanning...
[wifi] connected channel=8 rssi=-80            ← GOOD(2.4GHz に繋がる)
[espectre] traffic=wifi_raw (Null Data + ACK)
[status] wifi=on ready=0 ...                   ← キャリブレーション中(約30秒、動かさない)
[csi] gen_pps=100 rx_pps=0 accepted_pps=99 admitted_pps=99 occ=0.99   ← GOOD
[espectre] ready (キャリブレーション完了)
[event] motion / [line] pushed motion          ← 手を振ると push が届く
```

以降は handson-atoms3.md の「実機確認」と同じ(10/20/30秒の3段階 push)。

## トラブルシュート(C3 固有)

| 症状 | 対処 |
|---|---|
| `rst:0x3` が 0.2〜0.3秒周期で連発、アプリログが一切出ない | **bootloader 起因**(Step 2 をやり直す)。詳細は `logs/2026-11-18-c3-investigation.md` |
| モニタ open のたびに `rst:0x15 boot:0x4` で止まる | モニタ開放リセット罠。**USB 再挿し → 冷起動直後にモニタ**が安定。`monitor_rts=0/monitor_dtr=0` は platformio.ini 設定済み |
| モニタが開くたびに落ちる/頻繁に切断する | ホストで `uvx --from esptool esp_rfc2217_server -v -p 4000 /dev/cu.usbmodem101` を常駐 → コンテナ/Codespaces から `rfc2217://host.docker.internal:4000?ign_set_control` で接続(開放のリセットが起きない) |
| `scanNetworks` で見つからない | テザリングは「接続に使うWi-Fi設定」を 2.4GHz にしてから再起動 |

## ボード差メモ(AtomS3 / Echo との違い)

- 表示がシリアルログのみ(LCD/LED/ビープなし)
- 初回 bootloader 更新の必要性は Echo と同じ。**他プロジェクトで使った C3 では必ず発生**する
- ESPectre は問題なく C3 で動作する(起動ループは bootloader 由来)
