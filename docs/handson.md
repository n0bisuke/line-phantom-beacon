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
| 検証ボード | M5Stack AtomS3(本編)/ **ATOM Echo**(→ [handson-atom-echo.md](handson-atom-echo.md))。USB-C **データ通信対応**ケーブル |
| Wi-Fi(2.4GHz) | スマホのテザリング可。**5GHz は CSI が取れない**ので 2.4GHz に固定する |
| LINE Developers アカウント | Messaging API チャネル + 長期チャネルアクセストークン + 自分のユーザーID(`U`で始まる) |
| GitHub アカウント | Codespaces が使える課金プラン(無料枠60時間/月でも十分) |
| 書き込み用のホストPC | Codespaces から USB は触れないため。ホストには **PlatformIO CLI + agent** が必須。ATOM Echo など初回の直書きが必要なボードでは **uv**(uvx esptool)もあるとよい |

手元が AtomS3 で Wi-Fi が自宅 LAN の場合、書き込み直しは「ホストPCに PlatformIO+ツールチェーン」でも「`pio remote` 経由」でもOK。この手順はホストが軽い **`pio remote`** 方式で統一します。

### ホストPCの事前準備

```bash
# PlatformIO CLI(=pio)+ agent の準備。全ボード共通で必須
pip install -U platformio
pio account login          # https://platformio.org の無料アカウント(トークン方式でも可)

# ATOM Echo の初回書き込み(統合バイナリ直書き)をやる場合のみ。uv で esptool を持つ
curl -LsSf https://astral.sh/uv/install.sh | sh
# (uv を使わない代替: pip install esptool して esptool コマンドを直接使う)

# agent を常駐させておく(書き込みとモニタはここを経由する)
pio remote agent start
```

## Step 0: ボードを決める(複数ボード対応)

AtomS3 以外のボードにも対応しています。ビルドターゲットは `pio run -e <env>` / `pio run -t upload -e <env>` で選びます。

| env(`-e`) | ボード | LCD | フラッシュ | 備考 |
|---|---|---|---|---|
| `m5stack-atoms3`(既定) | M5Stack AtomS3 | あり | 8MB | LCD状態表示あり・実機検証済み |
| `m5stack-atom` | **ATOM Echo/Matrix/Lite** | なし | 4MB | ビープ+LED。**→ [handson-atom-echo.md](handson-atom-echo.md)** |
| `seeed_xiao_esp32s3` | XIAO ESP32-S3(+Plus/Sense) | なし | 8MB | 状態はシリアルログで確認 |

> **Codespaces を使わずローカルだけで完結したい人は**下の
> 「**ローカルPCだけで完結する場合(Codespaces不要)**」セクションへ(clone→build→upload→monitor)。

- ディスプレイなしボードはヘッドレスモード(`PHANTOM_NO_DISPLAY`)になり、起動ステージと状態を**シリアルログで確認**します
- ⚠️ **XIAO ESP32-C3 は未対応**: ESPectre 3.2.0 × C3(Arduino core 3.3.12)で起動早期にペニックする既知問題
  (センサーの有無・トラフィック生成モードに関係なく再現。詳細は `logs/2026-11-18-development-log.md`)
- その他のボードも `platformio.ini` に env を足すだけで基本OK(M5Stack 系は LCD ありにする)

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

### ⚠️ LINE 無料プランの「月200メッセージ」に注意

- LINE Messaging API の**無料プランは1チャネルあたり**push に使えるメッセージが**月200通まで**
- このハンズオンは **push 通知自体をテストするもの**なので、検証で枠を消費します(1回の検証サイクルで5〜10通程度)
- 消費すると LINE 側は **HTTP 429** を返し、シリアルログは `[line] push ... failed: 429` になる。**トークンは生きているが上限到達**の状態
- 枠は**毎月1日にリセット**。急ぎの検証なら「開発用に別チャネルを作る」「有料プラン」のいずれか
- 消費を抑えるコツ: 通知テキストを変える → push は1文ごとに1通。しきい値を短めにしても push 通数自体は変わらない。**同じ検証を何度も回さない**のが一番確実
- 現在の使用状況は LINE Developers コンソール → 対象チャネル → **「Messaging API設定」**で確認できる

## Step 3: ビルドとテスト

```bash
pio run              # ファームウェアビルド(既定 = m5stack-atoms3)
                     # 他ボード: pio run -e seeed_xiao_esp32s3 / -e m5stack-atom
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
pio remote run -t upload   # ビルド済みファームを書き込み(他ボードは -e <env> を追加)
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

LCD あり(AtomS3)の場合:

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

LCD なし(XIAO ESP32-S3 系)の場合、同じ内容を**シリアルログ**で確認する:

1. 起動直後に `[display] stage 0: BOOT → 1: WIFI → 2: RUN` が流れる(LCD の代わり)
2. 黄 `CALIB` の代わりは `ready=0` の期間(約30秒、動かさない)
3. `[espectre] ready (キャリブレーション完了)` → その後の `[motion] edge` で動きを確認
4. `[event]` / `[line] pushed` の流れは全ボード共通(下の表を検収基準にする)

シリアルログの見所:

```
[status] wifi=on  ready=1 motion=0 movement=0 idle_ms=1200 count=1 ...
[csi]    gen_pps=100 rx_pps=0 accepted_pps=100 admitted_pps=100 occ=1.00   ← CSI が流れている
[event]  motion / pseudo_leave / pseudo_leave_continued / sleep_suspected
[line]   pushed pseudo_leave
```

`admitted_pps` が 0 付近なら CSI が流れていない(WPA3-only や 5GHz、AP の問題)。

## Step 7: ハンズオン向けお題(余裕があれば)

### しきい値を変えてみる(10秒 / 20秒 / 30秒)

動きがなくなってからの3段階の通知タイミングは、**`include/secrets.h` を編集するだけで変えられます**:

```c
// ---- 疑似イベントしきい値 ----
#define PSEUDO_LEAVE_MS      10000UL  // 10秒で「動きがなくなりました」
#define PSEUDO_CONTINUED_MS  20000UL  // 20秒で「継続して動きがありません」
#define SLEEP_SUSPECTED_MS   30000UL  // 30秒で寝落ち(この後は push しない)
```

- 例: 短くしたければ `5000 / 8000 / 15000UL` など。**単位はミリ秒**
- 変えると **push 文言の「(○秒無動作・擬似)」も自動で追従**します
- 逆の制約: `PSEUDO_LEAVE_MS < PSEUDO_CONTINUED_MS < SLEEP_SUSPECTED_MS` の順序だけ守る
- 手順: secrets.h を編集 → `pio run`(ボードごとに `-e` 指定)→ 書き込み → 実機確認。
  変更は git にコミットされません(secrets.h は .gitignore 済み)

### その他の変更

1. push 文言を変える → `src/line_push_client.cpp` の `makeText()`
2. LCDの色を変える → `src/display.cpp` の `color565(...)`
3. Echoのビープ/LEDを変える → `src/display.cpp` のヘッドレス分岐(`PHANTOM_SPEAKER_PIN` / `PHANTOM_LED_PIN`)
4. `lib/phantom_events/` のネイティブテストに自分のテストを追加 → `pio test -e native`
5. LINE Simple Beacon の BLE 送信(Phase 2)を実装する → `lib/line_beacon_frame/` がユニットテスト付きで用意済み

## ローカルPCだけで完結する場合(Codespaces不要)

GitHub アカウントや Codespaces を使わず、USB の繋がる PC 1台で全部やるルートです。
ホスト事前準備の `pip install -U platformio`(+ 初回のツールチェーン取得)だけ済ませればOK。

```bash
# 0. リポジトリ取得(fork 不要。public リポジトリなら clone だけ)
git clone https://github.com/<ユーザー名>/line-phantom-beacon.git
cd line-phantom-beacon

# 1. 秘密情報(Step 2 と同じ内容:Wi-Fi / LINEトークン / USER_ID)
cp include/secrets.h.example include/secrets.h
your-editor include/secrets.h

# 2. ビルドとテスト
pio run                  # 既定 = m5stack-atoms3(他ボードは -e <env> を付ける)
pio test -e native       # ボード不要のユニットテスト(gcc が必要)

# 3. 書き込みとモニタ(agent 不要。ボードを USB 接続して実行)
pio run -t upload                 # ← ボード名が1種だけなら -e は不要
pio run -t upload -e m5stack-atom # ← 複数envがある場合の例
pio device monitor                # Ctrl+C で終了
```

- **メリット**: 書き込み・モニタのサイクルが一番速い。リモート/クラウドが不要
- **注意**:
  - 初回の `pio run` は ESP32 ツールチェーンのダウンロードで 10分以上かかる
  - **AtomS3(XIAO S3 も同類)**は USB-Serial-JTAG なので、稼働中のボードに対して
    `pio device monitor` を open すると `rst:0x15 boot:0x20 DOWNLOAD` で
    ダウンロードモードに落ちて画面が暗くなることがある
    (platformio.ini の `monitor_rts=0/monitor_dtr=0` で大抵回避。落ちたら USB 抜き差し)
  - **ATOM Echo** は初回のみ統合バイナリ直書きが必要(→ [handson-atom-echo.md](handson-atom-echo.md) の Step 2。
    ローカルなら esptool は同じPCに入っていて良いので、`uvx esptool write-flash …` で完結する)
  - ソースを `git clone` するとき `include/secrets.h` はリポジトリに無い(.gitignore 済み)ので
    example から作る。実機で書き換えたしきい値は secrets.h 内なので git に載らない

上級者向けの細かい手順(merged 統合バイナリの作り方など)は下の「代替手順」を参照。

## 代替手順: Codespaces / pio remote を使わない書き込み

USB に触れられるPCに PlatformIO を入れるなら通常フローです:

1. リポジトリを clone し、Step 2 の secrets.h を書く
2. `pio run -t upload`(初回ツールチェーン10分+)
3. `pio device monitor`

pio remote を使わない場合: `pio run` の成果物(`.pio/build/<env>/firmware.bin` 等)を
merge する方法もあります(esptool 5.x は `merge-bin` / `write-flash` のハイフン付き表記):

```bash
esptool --chip esp32s3 merge-bin -o merged-firmware.bin \
  --flash_mode dio --flash_size 4MB \
  0x1000  .pio/build/m5stack-atoms3/bootloader.bin \
  0x8000  .pio/build/m5stack-atoms3/partitions.bin \
  0x10000 .pio/build/m5stack-atoms3/firmware.bin
esptool --chip esp32s3 write-flash 0x0 merged-firmware.bin --port <ポート名>
```

Codespaces で merge しておいて、`merged-firmware.bin` を PC に落としてから書くという使い方も可能です。

## ボード別の検収基準(複数ボードでの検証用)

各ボードで同じチェックを通す。全部シリアルログで確認できる:

| # | チェック | 期待されるログ |
|---|---|---|
| 1 | Wi-Fi 接続 | `[wifi] connected channel=8 rssi=-56` |
| 2 | CSI が流れる | `[csi] admitted_pps=80〜100 occ=0.8以上` |
| 3 | キャリブレーション完了 | `[espectre] ready` |
| 4 | 動き検知 | 手を振る → `[motion] edge` / `[event] motion` / `[line] pushed motion` |
| 5 | 10段階 | 10秒後 `[event] pseudo_leave`(LCDなら暗青+LEAVE) |
| 6 | 20/30段階 | 20秒 `pseudo_leave_continued` / 30秒 `sleep_suspected`、以降 `[event]` なし |
| 7 | 検知復帰 | 手を振る → `[event] motion`、LCDなら緑に復帰 |

C3 など未検証ボードで `occ` が 0 のままなら、ボードによって CSI の実装差(802.11n / チャネル)に起因する可能性あり → `docs/blog.md` の「CSI が流れない」節と同じ切り分けをする。

## トラブルシュートまとめ

| 症状 | 原因と対処 |
|---|---|
| Calib から動かない / `ready=0` のまま | CSI が流れていない。`occ` を確認、`admitted_pps` ゼロなら 2.4GHz/802.11n 必須、WPA3 独占 AP は不可 |
| LCD に `NO-WIFI` | Wi-Fi 未接続。SSID/パスワード、5GHz を疑う |
| 動きは検知されるが push が来ない | LINE の使用状況を確認: **429=月200通の上限**(毎月1日リセット)/ 401=トークン失効。シリアルで `[line]` を確認 |
| 複数人で同じ Wi-Fi に AtomS3 を付けるとおかしい | 未検証。CSI は距離・位置で感度が大きく変わります |
| テザリング時、動きが検知されない | テザリングは ICMP を弾くことがあるが wifi_raw で回避済み。むしろデバイスが 5GHz に居ないか確認 |

## 秘密情報の取り扱い

- `include/secrets.h` / `.env` は gitignore済み。`git status` で「untracked に載る」のが正しい状態
- 公開手順は README、経緯(なぜこうなったか)は [docs/blog.md](blog.md) を参照
