# ハンズオン: line-phantom-beacon を GitHub Codespaces でビルドして動かす

AtomS3 + Wi-Fi電波(CSI)で「動きを検知 → LINE push」する疑似ビーコンのハンズオンです。
理論・経緯は [docs/blog.md](blog.md)、仕様詳細は [README.md](../README.md) を参照。

## 完成イメージ

1. AtomS3 が Wi-Fi の CSI(チャネル状態情報)を解析して動きを検知
2. 検知直後 → 🏃 push
3. 動きがなくなる → 10秒後 🚪 push / 20秒後 継続push / 30秒後 😴 push(以降なし)
4. LCD にも状態表示(緑 MOVE / 暗青 LEAVE / 紫 SLEEP / 赤 NO-WIFI)

## 前提(必要なもの)

| 用意するもの | 備考 |
|---|---|
| M5Stack AtomS3 | 本体単体。USB-C **データ通信対応**ケーブル |
| Wi-Fi(2.4GHz) | スマホのテザリング可。**5GHz は CSI が取れない**ので 2.4GHz に固定する |
| LINE Developers アカウント | Messaging API チャネル + 長期チャネルアクセストークン + 自分のユーザーID(`U`で始まる) |
| GitHub アカウント | Codespaces が使える課金プラン(無料枠60時間/月でも十分) |
| 書き込み用のホストPC | Codespaces から USB は触れないため。PlatformIO CLI が入れられる PC |

手元が AtomS3 で Wi-Fi が自宅 LAN の場合、書き込み直しは「ホストPCに PlatformIO+ツールチェーン」でも「`pio remote` 経由」でもOK。この手順はホストが軽い **`pio remote`** 方式で統一します。

## Step 1: リポジトリを fork → Codespaces を開く

1. このリポジトリを GitHub で fork / もしくは自分の organization にコピー
2. 「Code → Codespaces → Create codespaces on main」
3. コンテナビルド後、`postCreateCommand` で `pio pkg install` が走る
   - **初回は ESP32 ツールチェーン(約3GB)のダウンロードで 10分以上かかります**。止まったように見えても待つ
4. 動作確認:

   ```bash
   pio --version          # 6.2.0 系
   pio run -e native      # ユニットテスト用 native env の確認(先にテストだけ回すなら)
   ```

## Step 2: 秘密情報の設定(secrets.h)

```bash
cp include/secrets.h.example include/secrets.h
code include/secrets.h
```

書くのは3つ:

| 定義 | 値 |
|---|---|
| `WIFI_CREDENTIALS` | `{ "SSID", "パスワード" }`(2.4GHz。カンマ区切りで複数AP登録可) |
| `LINE_CHANNEL_TOKEN` | 長期チャネルアクセストークン(172字) |
| `LINE_USER_ID` | 自分のユーザーID(`U` で始まる34字) |

> `include/secrets.h` は **.gitignore 済み**。コミットしないこと。トークンをうっかり push したら LINE Developers コンソールで即ローテーション。

push テキストや端末名は `DEVICE_ID`、10/20/30秒のしきい値は `PSEUDO_LEAVE_MS` / `PSEUDO_CONTINUED_MS` / `SLEEP_SUSPECTED_MS` で変更できます(文言の秒数は自動で追従)。

## Step 3: ビルドとテスト

```bash
pio run              # ファームウェアビルド(2回目以降は速い)
pio test -e native   # 状態遷移・BEACONフレームのユニットテスト(ボード不要)
```

期待: `SUCCESS` / テスト 17/17。Flash 使用量は約 55%。

## Step 4: ホストPCに pio agent を起動(書き込みの準備)

Codespaces から USB は直接触れないので、**ホストPCの agent 経由**で書き込みます。

ホストPC(macOS / Windows / Linux のいずれか、AtomS3 と同じ Wi-Fi にいる必要なし):

```bash
pip install -U platformio
pio account login          # LINE ではなく PlatformIO のアカウント(無料登録可)
pio remote agent start     # 起動したまま置いておく
```

> PlatformIO アカウントは https://platformio.org で無料登録。トークン方式でも可:
> `PLATFORMIO_AUTH_TOKEN=<token> pio remote agent start`

> ⚠️ファームに Wi-Fi パスワードや LINE トークンを埋め込む場合でも、そのバイナリは PlatformIO
> のクラウドを経由して agent に届きます。心配なら Step 6(代替手順)を使ってください。

## Step 5: 書き込みとモニタリング(Codespaces 内)

Codespaces のターミナルで:

```bash
pio account login          # Step 4 と同じ PlatformIO アカウントで(対話でOK)
pio remote agent list      # Step 4 の agent が見えるか確認
pio remote run -t upload   # ビルド済みファームを書き込み
pio remote device monitor  # シリアルモニタ(Ctrl+C で終了)
```

### うまくいかないとき

- `No serial data received` → USB ケーブルを一度抜き差しして再試行
- モニタopen直後に `rst:0x15 ... boot:0x20 DOWNLOAD(USB/UART0)` が出て画面が暗いままになったら
  **USBダウンロードモード待ちに落ちている罠**(`monitor_rts=0/monitor_dtr=0` で大概回避できますが
  タイミングで起きます)。回避: ボードが ROM 書き込み待ちでも `pio remote run -t upload` は効くので、
  もう一度書き込むとリスタートします
- `Association refused too many times` テザリング相手は最初の1〜2回拒むことがある。リトライで接続

## Step 6: 実機で確認

1. 起動 → LCD に `BOOT` → `WIFI` → `RUN` の段階番号
2. 黄色 **`CALIB`** が約30秒(途中で動かさない。これで「何もない部屋」の基準を作る)
3. 正常監視: 黒背景の秒数カウント。上段 `W:on R1 mv:...`
4. 手を振る → 緑 **`MOVE`** → LINE に 🏃 push
   - 緑は動きが止んでも**数秒残る**(ESPectre のデバウンス)。それが仕様です
5. 次に完全に静止 → 緑が消えた瞬間から 0 に戻って再カウント
   - **10秒ちょうど** → 🚪 push + LCD 暗青 + 上段 `LEAVE`
   - **20秒** → 「継続して動きがありません」push
   - **30秒** → 😴 push + LCD 紫 + 上段 `SLEEP`(カウントは延長戦で続く)
   - 30秒以降は push しない
6. 再び動かす → 緑 `MOVE` に復帰

シリアルログの見所:

```
[status] wifi=on  ready=1 motion=0 movement=0 idle_ms=1200 count=1 ...
[csi]    gen_pps=100 rx_pps=0 accepted_pps=100 admitted_pps=100 occ=1.00   ← CSI が流れている
[event]  motion / pseudo_leave / pseudo_leave_continued / sleep_suspected
[line]   pushed pseudo_leave
```

`admitted_pps` が 0 付近なら CSI が流れていない(WPA3-only や 5GHz、AP の問題)。

## Step 7: ハンズオン向けお題(余裕があれば)

1. 「30秒で寝落ち」を「60秒に」変える → `SLEEP_SUSPECTED_MS`
2. push 文言を変える → `src/line_push_client.cpp` の `makeText()`
3. LCDの色を変える → `src/display.cpp` の `color565(...)`
4. `lib/phantom_events/` のネイティブテストに自分のテストを追加 → `pio test -e native`
5. LINE Simple Beacon の BLE 送信(Phase 2)を実装する → `lib/line_beacon_frame/` がユニットテスト付きで用意済み

## 代替手順: Codespaces / pio remote を使わない書き込み

USB に触れられるPCに PlatformIO を入れるなら通常フローです:

1. リポジトリを clone し、Step 2 の secrets.h を書く
2. `pio run -t upload`(初回ツールチェーン10分+)
3. `pio device monitor`

pio remote を使わない場合: `pio run` の成果物(`.pio/build/m5stack-atoms3/firmware.bin` 等)を
持ってきて merge する方法もあります:

```bash
esptool --chip esp32s3 --baud 921600 merge_bin -o merged-firmware.bin \
  --flash_mode dio --flash_size 4MB \
  0x0     .pio/build/m5stack-atoms3/bootloader.bin \
  0x8000  .pio/build/m5stack-atoms3/partitions.bin \
  0x10000 .pio/build/m5stack-atoms3/firmware.bin
esptool --chip esp32s3 --baud 921600 write_flash 0x0 merged-firmware.bin
```

Codespaces で merge しておいて、`merged-firmware.bin` を PC に落としてから書くという使い方も可能です。

## トラブルシュートまとめ

| 症状 | 原因と対処 |
|---|---|
| Calib から動かない / `ready=0` のまま | CSI が流れていない。`occ` を確認、`admitted_pps` ゼロなら 2.4GHz/802.11n 必須、WPA3 独占 AP は不可 |
| LCD に `NO-WIFI` | Wi-Fi 未接続。SSID/パスワード、5GHz を疑う |
| push が来ない | LINE 側: トークン失効(401)/無料プランの push 上限。シリアルで `[line]` を確認 |
| 複数人で同じ Wi-Fi に AtomS3 を付けるとおかしい | 未検証。CSI は距離・位置で感度が大きく変わります |
| テザリング時、動きが検知されない | テザリングは ICMP を弾くことがあるが wifi_raw で回避済み。むしろデバイスが 5GHz に居ないか確認 |

## 秘密情報の取り扱い

- `include/secrets.h` / `.env` は gitignore済み。`git status` で「untracked に載る」のが正しい状態
- 公開手順は README、経緯(なぜこうなったか)は [docs/blog.md](blog.md) を参照
