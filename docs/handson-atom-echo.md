# ハンズオン: M5Stack ATOM Echo で line-phantom-beacon を動かす

ATOM Echo は**LCDが無い・クラシックESP32(PICO-D4 4MB)・圧電スピーカー付き**のデバイスです。
本体手順は [handson.md](handson.md) を参照。この文書は **ATOM Echo 固有の違い**だけを記載します。

> 作業環境はどちらでもOK: **Codespaces/コンテナ**(→ handson.md 本編)にも
> **ローカルPC完結**(→ handson.md「ローカルPCだけで完結する場合」)にも対応。
> ビルドコマンド(`pio run -e m5stack-atom`)とテストは共通で、**書き込みだけ方式が違う**(Step 2-3)。

## AtomS3 と何が違うか

| 項目 | AtomS3(既定) | ATOM Echo |
|---|---|---|
| コア | ESP32-S3(ネイティブUSB) | **ESP32 クラシック**(USB-UARTブリッジ) |
| ディスプレイ | LCD あり | **なし → シリアルログ + ビープ音 + 内蔵LEDの色** |
| 書き込み | pio remote で完結 | **初回のみ統合バイナリ直書き**(下記) |
| env | `m5stack-atoms3` | `m5stack-atom` |

- CSI 検知ロジック・疑似イベント・LINE push は**どのボードでも同一**
- ATOM Echo 用ビルド: `pio run -e m5stack-atom`(Flash 44.8%)
- LINE push には **`(atom-echo)`** という端末ラベルが付く(Echo env で `PHANTOM_DEVICE_ID=atom-echo`)

## ホストPCの事前準備(両方入れる)

| ツール | 用途 | 導入 |
|---|---|---|
| PlatformIO CLI | `pio remote agent start`(参加者は Codespaces から書き込む際の足場) | `pip install -U platformio`(macOSなら Homebrew 也可) |
| uv | **初回の統合バイナリ直書き**で `uvx esptool` を使う | `curl -LsSf https://astral.sh/uv/install.sh \| sh` |

- どちらも1回でOK。`pio account login` も初回のみ必要
- uv を入れたくない場合、直書きは `pip install esptool` でも可能(`esptool` コマンドが直接使える。バージョンは 5.x)

## ビープ音 + LED 仕様(キャリブレーション中か起動中かはLEDの色で判別)

> ⚠️ **ビープ音は既知問題あり(2026-11-18 現在)**: スピーカーは Class-D アンプ(DAC/GPIO25駆動)で、
> LEDC PWM では鳴らないことが実機で判明。DAC + esp_timer 方式に変更したが、
> **実機で鳴るかは未確定**(サイレントな場合あり)。LED は正常動作。
> 音が鳴らなくても検知・push・LED はすべて正常。詳細は開発ログ `logs/2026-11-18-development-log.md`。

| LEDの色 | 音 | 状態 |
|---|---|---|
| 白(dim) | 起動3連短音(未解決) | **起動中** ※実機確認済み |
| 青 | 3秒ごとの低いピッ(未解決) | **キャリブレーション中**(約30秒。動かさない) |
| --- | ピロリン(未解決) | キャリブレーション完了 → 次の色へ ※実機確認済み |
| 緑(dim) | --- | 正常監視中 |
| 赤 | --- | 動きを検知中 |
| シアン | --- | 疑似 leave(10秒静止〜) ※10秒で🚪push |
| 紫 | --- | 寝落ち(30秒〜) ※LCD版と同じ配色 |
| オレンジ | --- | Wi-Fi 未接続 |

### ATOM Matrix / Lite について

- 同じ `m5stack-atom` env・`merged-firmware.bin` の流れがそのまま使える(Echo と同ファミリ)
- **スピーカーは無い**のでビープなし(GPIO25 への PWM 出力は Grove ピンに出るため、
  Matrix/Lite 用には `PHANTOM_SPEAKER_PIN` を付けない方が安全)
- 5×5 RGB LED マトリクスがあるため、状態表示を LED に置き換える実装が有望(未実装)

## Step 1: ビルド(Codespaces でもローカルでも同じ)

```bash
pio run -e m5stack-atom
```

## Step 2: 初回のみ「統合バイナリ」をホストから直書き

**理由**: `pio remote run -t upload` はアプリ領域(0x10000)のみ書き込みます。
工場出荷状態の Echo には「アプリ1MB」のパーティションテーブルが既に入っており、
CSI ファーム(約1.4MB)が入らないため、パーティションの書き換えが必要です。

1. Codespaces/コンテナ内で統合バイナリを作る(bootloader+partitions+app を1つに):

   ```bash
   esptool --chip esp32 merge-bin -o .pio/build/m5stack-atom/merged-firmware.bin \
     --flash_mode dio --flash_size 4MB \
     0x1000  .pio/build/m5stack-atom/bootloader.bin \
     0x8000  .pio/build/m5stack-atom/partitions.bin \
     0x10000 .pio/build/m5stack-atom/firmware.bin
   ```

   → `HINT: ... Wrote 0x... bytes ... ready to flash to offset 0x0`

2. ホストPC(macOS/Linux、Echo をUSB接続)で直書き:

   ```bash
   cd <このリポジトリをcloneした場所>
   uvx esptool --chip esp32 --port /dev/cu.usbserial-XXXXXXXX write-flash 0x0 \
     .pio/build/m5stack-atom/merged-firmware.bin
   ```

   - **ローカル開発ルート(Codespaces 不要)ではここが主な書き込み手段になります**。
     Codespaces ルートでもこの1回だけはホスト側で実施が必要です
   - ポート名は `ls /dev/cu.*` で確認(Echo は `cu.usbserial-…` 形式: USB-UARTブリッジ)
   - `921600` ボーの指定は **しない**こと(Echo のブリッジで帯域エラーが出る。既定115200で約76秒)
   - ソースはコンテナ/Codespaces とホストで共有されている(cloneした同一リポジトリ)

3. `Hash of data verified.` まで進めば完了。**以後の更新は Step 3 だけでよい**

## Step 3: 通常運用(pio remote: Codespaces から or ローカル)

ファームを変えて再検証するとき。Codespaces からは pio remote(agent)、**ローカルのみなら
esptool 直書きがそのまま使える**:

```bash
# Codespaces/コンテナから(agent + ホスト側 pio account login が必要)
pio account login                                   # 初回のみ
pio remote run -t upload -e m5stack-atom            # アプリ領域のみ書き換え(約30秒)
pio remote device monitor                           # シリアルモニタ(Ctrl+C で終了)

# ローカルPCだけで完結する場合(esptool 5.x、ポート名は ls /dev/cu.* で確認)
uvx esptool --chip esp32 --port /dev/cu.usbserial-XXXXXXXX write-flash 0x10000 \
  .pio/build/m5stack-atom/firmware.bin              # ← アプリ領域のみなので高速
pio device monitor --port /dev/cu.usbserial-XXXXXXXX
```

- ソース変更 → `pio run -e m5stack-atom` → 書き込み の繰り返しで完結。**Step 2 の直書きは二度と不要**
- Codespaces ルートではホストは `pip install -U platformio && pio account login && pio remote agent start` の3行で準備
- ボードが工場出荷状態に戻った場合(消去した場合など)のみ Step 2 に戻る

## Step 4: 実機確認(LCDなし版)

1. ビープ3連(起動)→ Wi-Fi 接続: `[wifi] connected channel=... rssi=...`
2. 3秒ごとのピッ(キャリブレーション中。この間は**動かさない**)
3. ピロリン → `[espectre] ready (キャリブレーション完了)`
4. Echo に向かって手を振る → `[motion] edge` → `[event] motion` → LINE 🏃push
5. 静止 → 10秒で `[event] pseudo_leave` / 20秒 continued / 30秒 sleep_suspected、以降 push なし
6. 再び動かす → `[event] motion` に復帰

> ⚠️ **LINE 無料プランは月200通まで**。検証サイクルで5〜10通ずつ消費し、超えると
> `[line] push ... failed: 429`(トークンは生きているが上限到達)。枠は毎月1日リセット。
> 詳細は [handson.md](handson.md) の「LINE 無料プランの月200メッセージに注意」を参照。

CSI の目安(検収基準):

```
[csi] gen_pps=100 rx_pps=0 accepted_pps=93 admitted_pps=93 occ=0.93   ← GOOD
```

`admitted_pps` が 0 付近なら CSI が流れていない(2.4GHz 以外の AP、Wi-Fi 切断を疑う)。

## トラブルシュート(Echo 固有)

| 症状 | 対処 |
|---|---|
| 起動ログに `Factory app partition is invalid` が繰り返し出る | **パーティションが古い**。Step 2(統合バイナリ直書き)をやり直す |
| `Unable to verify flash chip connection` で書き込み失敗 | ボーレートが高すぎる。`--baud` 指定を外して既定115200で |
| ポート名が `cu.usbmodem…` でない | 正常(Echo は USB-UART ブリッジなので `cu.usbserial-…`) |
| ビープが鳴らない | スピーカーは GPIO25 **DAC駆動**(Class-Dアンプ)。LED の PWM では鳴らないため本プロジェクトでは DAC 出力を実装済み。`[display] speaker via DAC` が出てるか確認 |
| push が来ない(429) | **月200通上限**。毎月1日リセット。`handson.md` を参照 |
| 検知が常に動かない | `occ` を確認。Echo と AP の距離・人影を変えてみる |

## Codespaces からの書き込み方式の比較(参考)

| 方式 | 使える操作 | 備考 |
|---|---|---|
| `pio remote`(agent) | アプリ書き込み ✓ / モニタ ✓ | 参加者フローはこれ。初回だけ Step 2 の直書き |
| `esp_rfc2217_server` トンネル | 統合バイナリ含む全領域書き込み ✓ / モニタ ✓ | ホストで `uvx --from esptool esp_rfc2217_server -v -p 4000 /dev/cu.usbserial-…` を常駐 → コンテナから `rfc2217://host.docker.internal:4000`。USB-UARTブリッジボード(Echo 型)はブリッジ越しでリセット挙動が安定し使える。ただし参加者ごとの常駐が必要 |
| ホスト直書き | 全操作 | Step 2 の方式 |
