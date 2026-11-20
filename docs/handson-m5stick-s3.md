# ハンズオン: M5Stack StickS3 で line-phantom-beacon を動かす(ローカル PC / Codespaces 対応)

M5Stack StickS3 は **ESP32-S3FN8(8MB)+ 1.9インチ 320×170 LCD + スピーカー/マイク/カメラ** を備えた
小型 AI スティック型デバイスです。この文書だけで完結します(AtomS3 用は [handson-atoms3.md](handson-atoms3.md))。

> 検証状態(2026-11-19): ビルド/統合バイナリ直書きで起動。LCD・CSI・LINE push の確認は手順どおりに
> 実施してください(下記 Step 3)。

## 完成イメージ

1. StickS3 が Wi-Fi の CSI を解析して動きを検知(他ボードと同一ロジック、トラフィックは WIFI_RAW)
2. 検知直後 → 🏃 push(文末ラベル **`(sticks3)`**)
3. 10秒無動作 → 🚪 push / 20秒 継続push / 30秒 😴 push(以降なし)
4. **320×170 LCD に状態表示**(AtomS3 と同じ状態表示が、大きい画面に横伸びで表示されます)

## このボード特有の前提

| 項目 | 内容 |
|---|---|
| ボード定義 | PlatformIO 標準(espressif32 8.0.0)に **`m5stack-sticks3` は存在しないため、 AtomS3 定義(同じ ESP32-S3FN8/8MB)を流用**(`platformio.ini` の env `m5stack-sticks3`) |
| LCD | 1.9" 320×170(ST7701SN)。**M5Unified 0.3.0 が StickS3 を自動検出**(`board_M5StickS3` 対応済み)。表示座標は `width()` 可変対応済み |
| 工場出荷ファーム | **UIFlow が入っています**。本プロジェクトを焼くと置き換わります(元に戻すなら M5Burner) |
| 初回書き込み | このボードでは **`pio remote` の転送が滞留する問題が観測されている**ため、**ホストの esptool 直書きを推奨**(Step 2) |
| スピーカー/マイク | 搭載(NS4150/SPH0645)。現コードでは未使用(将来ビープや監視音に利用可能) |

## Step 1: 秘密情報 → ビルド(Codespaces でもローカルでも同じ)

共通準備(LINE Bot・キー・ESPectre 解説)は [handson-junbi.md](handson-junbi.md) を先に済ませてください。

1. LINE のアクセストークンとユーザーID、Wi-Fi 情報を secrets.h に入れ、ファームをビルドします。**LINE 準備がまだなら共通準備ガイド [handson-junbi.md](handson-junbi.md) の STEP 1 を先に。**

```bash
cp include/secrets.h.example include/secrets.h
code include/secrets.h        # (ローカルならお好みのエディタでOK)
pio run -e m5stack-sticks3    # StickS3 用ファーム(Flash 55.0%)
pio test -e native            # ロジックのユニットテスト(17/17)
```

## Step 2: 書き込み(このボードはホスト esptool 直書きを推奨)

📌 **直書きの前に: ボードをホスト PC と USB 接続してください**(データ通信用ケーブル。LED点灯確認)。
ポート名は `ls /dev/cu.*` で確認(USB直結チップなので `cu.usbmodem…` 形式)。

1. 統合バイナリ作成(Codespaces/コンテナ内、**初回のみ**):

   ```bash
   esptool --chip esp32s3 merge-bin -o .pio/build/m5stack-sticks3/merged-firmware.bin \
     --flash_mode dio --flash_size 8MB \
     0x0     .pio/build/m5stack-sticks3/bootloader.bin \
     0x8000  .pio/build/m5stack-sticks3/partitions.bin \
     0xe000  /root/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
     0x10000 .pio/build/m5stack-sticks3/firmware.bin
   ```

2. ホスト PC で直書き:

   ```bash
   cd <cloneしたリポジトリ>
   # 初回(bootloader+パーティション+アプリの全領域)
   uv esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX write-flash 0x0 \
     .pio/build/m5stack-sticks3/merged-firmware.bin
   # 2回目以降(アプリ領域だけ、高速)
   uv esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX write-flash 0x10000 \
     .pio/build/m5stack-sticks3/firmware.bin
   ```

   > 💡 **Codespaces でビルドした場合**: `.pio/build/**` は Codespace 上にしかありません。
   > `gh codespace cp -r <codespace名>:workspace/.pio/build/m5stack-sticks3/merged-firmware.bin .`
   > でホストへ取得してから焼いてください(**ローカルで `pio run` ならこの転送は不要**)。

3. USB 接続を維持してシリアルモニタ(ホスト単独):

   ```bash
   uv run --with pyserial python -m serial.tools.miniterm /dev/cu.usbmodemXXXX 115200
   ```

> 備考: `pio remote run -t upload`(agent 経由)は本ボードでは転送が滞留する問題が観測されています
> (2026-11-19 実測)。原因調査は今後の課題。手元 esptool が最速・確実です。

### ローカル PlatformIO での書き込み(一番シンプル・推奨)

ホスト PC に PlatformIO を入れていれば、**転送も統合バイナリも不要**でこの 1 コマンドです
(bootloader+パーティション+アプリが全領域焼かれ、4MB ボードの初回手順も自動で済みます):

```bash
pip install -U platformio     # ホストに未インストールの場合のみ(初回ビルドはツールチェーン約3GBで10分+)
pio run -t upload -e m5stack-sticks3    # ボードを USB 接続した状態で
pio device monitor            # シリアルモニタ(Ctrl+C で終了)
```

Codespaces でビルドした成果物を使う場合(転送あり)は上の Step 2 の手順を参照。
## Step 3: 実機確認(LCD)

**このSTEPでやること: ボードを設置して、LCD・シリアルログ・LINE 通知を確認します。**

1. 書き込み後の自動リセットで起動 → **LCD に `BOOT` → `WIFI` → `RUN`** の大型表示
   (320×170 向けに横が伸びて表示されます)
2. **約30秒 `CALIB`**(動かさない)
3. Wi-Fi 接続 → 黒背景の秒数カウント(正常監視中)
4. 手を振る → 緑 `MOVE` → LINE 🏃push(ラベル `(sticks3)`)
5. 静止 → 10秒で暗青 + LEAVE / 30秒で紫 + SLEEP(LCD 表示は AtomS3 と同じ仕組み)

シリアルログの検収基準:

```
[wifi] connected channel=8 rssi=-...
[espectre] traffic=wifi_raw (Null Data + ACK)      ← StickS3 は WIFI_RAW が使えます(C6 と違い DNS 不要)
[csi] gen_pps=101 rx_pps=0 accepted_pps=100 admitted_pps=90 occ=0.90
[event] motion / [line] direct push
```

## トラブルシュート(StickS3 固有)

| 症状 | 対処 |
|---|---|
| `pio remote` の upload が長時間帰ってこない | 既知問題(本ボード×agent 転送)。**ホスト esptool 直書き(Step 2)を使う** |
| 起動しても LCD 真っ暗 | M5Unified の StickS3 検出が効いていない可能性 → `pio pkg update -l m5stack/M5Unified` で新試行 / しばらくの間は手順どおり再ビルド |
| `rst:0x15 boot:0x0 DOWNLOAD waiting for download` で止まる | モニタ open もしくは esptool の hard reset が DOWNLOAD モードで終わった状態。**USB 再挿し**で通常起動、または再書き込み |
| LINE が届かない | secrets.h のトークン/ID と、Bot の友だち追加を確認(429 なら月200通の上限) |

## ボード差メモ

- 液晶は他ボードより大きい(320×170)が、実装としては M5Unified に任せるため AtomS3 と同じソースで動作
- カメラ/IMU/モーション(6軸)は本プロジェクトでは未使用。将来の拡張(例: カメラでの補助判定、
  IMU とのクロス検証)の余地あり
