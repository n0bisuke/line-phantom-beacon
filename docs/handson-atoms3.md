# ハンズオン: M5Stack AtomS3 で line-phantom-beacon を動かす(GitHub Codespaces / ローカル PC 対応)

AtomS3 は **LCD 付きの既定ボード**(実機検証済み)。検知状態が画面で見えるので、最初の 1 台はこれを推奨します。
共通準備(LINE Bot・キー・ESPectre 解説)は [handson-junbi.md](handson-junbi.md) を先に済ませておいてください。

他のボード用の資料: **ATOM Echo / Matrix / Lite → [handson-atom-echo.md](handson-atom-echo.md)**

## 完成イメージ

1. AtomS3 が Wi-Fi の CSI(チャネル状態情報)を解析して動きを検知
2. 検知直後 → 🏃 push
3. 動きがなくなる → 10秒後 🚪 push / 20秒後 継続push / 30秒後 😴 push(以降なし)
4. LCD にも状態表示(緑 MOVE / 暗青 LEAVE / 紫 SLEEP / 暗赤 NO-WIFI)

## 前提(必要なもの)

| 用意するもの | 備考 |
|---|---|
| M5Stack AtomS3 | USB-C **データ通信対応**ケーブル |
| Wi-Fi(2.4GHz) | スマホのテザリング可。**5GHz は CSI が取れない**ので 2.4GHz に固定 |
| LINE Developers アカウント | Messaging API チャネル + 長期チャネルアクセストークン + 自分のユーザーID(`U`で始まる) |
| GitHub アカウント | Codespaces を使う場合のみ |
| 書き込み用ホストPC | Codespaces ルートの場合、USB の繋がる PC(agent を常駐) |

ホストPCの事前準備:

```bash
pip install -U platformio   # 全ルートで共通(Agent用)
```

## Step 1: 作業環境を用意する

**このSTEPでやること: 作業環境を用意します。** Codespaces(ブラウザで完結)か ローカルPC のどちらかを決めて、その手順のみ実行します。

**A. Codespaces(推奨・チーム向き)**

1. リポジトリを fork → 「Code → Codespaces → Create codespaces on main」
2. コンテナビルド後、`postCreateCommand` で `pio pkg install` が走る
   - **初回は ESP32 ツールチェーン(約3GB)のダウンロードで 10分以上**(止まって見えても待つ)
3. 動作確認: `pio --version` → 6.2.0 系

**B. ローカル PC で完結(GitHub アカウント不要)**

```bash
git clone https://github.com/<ユーザー名>/line-phantom-beacon.git
cd line-phantom-beacon
pio run -e native   # ツールチェーン初回取得込みで動作確認(10分+)
```


> 注: `platformio.ini` の `default_envs`(既定 = AtomS3)は**書き換え不要**です。
> コマンドに `-e <自分のenv>` を付ければ常にそのボードでビルド/書き込みされます。

## Step 2: 秘密情報の設定(secrets.h)

**このSTEPでやること: LINE のチャネルアクセストークンとユーザーID、Wi-Fi の SSID/パスワードを secrets.h に入れます。** LINE準備がまだなら共通準備ガイド [handson-junbi.md](handson-junbi.md) の STEP 1 を先に。

```bash
cp include/secrets.h.example include/secrets.h
code include/secrets.h
```

> 💡 `code` は Codespaces(VS Code) の開き方。**ローカル参加者はいつも使っているエディタでOK**
> (nano / vim / GUI エディタ / GitHub Web 画面での編集など)。やることは同じです。

| 定義 | 値 |
|---|---|
| `WIFI_CREDENTIALS` | `{ "SSID", "パスワード" }`(2.4GHz。カンマ区切りで複数AP登録可) |
| `LINE_CHANNEL_TOKEN` | 長期チャネルアクセストークン(172字) |
| `LINE_USER_ID` | 自分のユーザーID(`U` で始まる34字) |

> `include/secrets.h` は **.gitignore 済み**。コミットしないこと。トークンをうっかり push したら LINE Developers コンソールで即ローテーション。

push テキストや端末名は `DEVICE_ID`、10/20/30秒のしきい値は `PSEUDO_LEAVE_MS` / `PSEUDO_CONTINUED_MS` / `SLEEP_SUSPECTED_MS` で変更できます(文言の秒数は自動で追従)。

### ⚠️ LINE 無料プランの「月200メッセージ」に注意

- LINE Messaging API の**無料プランは1チャネルあたり**push が**月200通まで**
- このハンズオンは push 通知自体をテストするので、検証で枠を消費します(1サイクル 5〜10通)
- 上限に達すると LINE は **HTTP 429**(`[line] push ... failed: 429`)。**トークンは生きているが上限到達**
- 枠は**毎月1日リセット**。急ぎなら開発用チャネルを分ける
- 使用状況: LINE Developers コンソール → 対象チャネル → 「Messaging API設定」

## Step 3: ビルドとテスト

**このSTEPでやること: ファームウェアをビルドし、ボードなしのPCでロジックの自動テスト(17件)を回します。** ここが全部緑ならあとは書き込むだけです。

```bash
pio run              # 既定 = m5stack-atoms3
pio test -e native   # 状態遷移・BEACONフレームのユニットテスト(ボード不要)
```

期待: `SUCCESS` / テスト 17/17。Flash 約 55%。

## Step 4: 書き込みとモニタ

**このSTEPでやること: ビルドしたファームを USB ケーブル経由でボードに焼き、シリアルログで動作を確認します。**

**A. Codespaces ルート(pio remote / agent 経由)**

ホストPC(USB の繋がる PC、agent を常駐):

```bash
pio account register      # アカウントを持っていなければ作成(既認証なら不要。Webでも可)
pio account login          # PlatformIO の無料アカウント(LINEではない)
pio remote agent start     # 起動したまま置いておく
```

**成功するとこの4行が出ます**(マシン名は環境ごとに異なります):

```
[info] Name: <マシン名>
[info] Connecting to PlatformIO Remote Development Cloud
[info] Successfully connected
[info] Authenticating
[info] Successfully authorized
```

Codespaces 側:

```bash
pio account login          # 初回のみ(同アカウント)
pio remote agent list      # agent が見えるか確認
p io remote run -t upload  # 書き込み
pio remote device monitor  # シリアルモニタ(Ctrl+C で終了)
```

**B. ローカル PC ルート(agent 不要)**

```bash
pio run -t upload     # ボードを USB 接続した状態で
pio device monitor    # Ctrl+C で終了
```

### 方式 C: esptool 直書き(2回目以降のアプリ領域書き換えをツールチェーンなしで行う)

⚠️ **Codespaces でビルドした場合、「ホストだけで完結」はできません**: ビルド成果物を Codespace
からホストへ運ぶ必要があります(下記 gh codespace cp)。ファイル転送なしで完結させたい場合は
ローカルに PlatformIO を入れて `pio run -t upload`(方式A/B)を使ってください。

⚠️ **前提: 標準の Codespaces ではビルド成果物(.pio/build/**)はホスト PC には存在しません**
(ビルドは Codespaces 上のクラウド環境で行われるため。OrbStack 等のホスト共有 Docker と違い、
ファイルの自動共有はありません)。ホストに持ってくるには **gh CLI の codespace cp** を使います:

```bash
# ホストで: gh を入れて認証(初回のみ)
#   macOS: brew install gh && gh auth login
gh codespace list                       # Codespaces の名前を確認
gh codespace cp -r <codespace名>:workspace/.pio/build/m5stack-atoms3/firmware.bin .
gh codespace cp -r <codespace名>:workspace/.pio/build/m5stack-atoms3/bootloader.bin .
gh codespace cp -r <codespace名>:workspace/.pio/build/m5stack-atoms3/partitions.bin .
```

(あるいは **ホストで PlatformIO を入れてローカルビルド**すれば転送は一切不要:

```bash
pip install -U platformio
pio run -t upload        # ← これ1つで bootloader+パーティション+アプリを全部焼く
```

)
ホストにファイルが揃った後の焼き方:

```bash
# (任意)統合バイナリを作る場合(bootloader+パーティションもまとめて)
esptool --chip esp32s3 merge-bin -o .pio/build/m5stack-atoms3/merged-firmware.bin   --flash_mode dio --flash_size 8MB   0x0     .pio/build/m5stack-atoms3/bootloader.bin   0x8000  .pio/build/m5stack-atoms3/partitions.bin   0xe000  /root/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin   0x10000 .pio/build/m5stack-atoms3/firmware.bin

# ホスト: 2回目以降は「アプリ領域だけ」でよい(bootloader は既にボードに入っている)
uv esptool --chip esp32s3 --port /dev/cu.usbmodemXXX write-flash 0x10000   .pio/build/m5stack-atoms3/firmware.bin

# (初回/パーティション変更があった場合のみ merged を 0x0 から焼く)
```

Codespaces とホストはリポジトリが共有されているため、上記のファイルパスがそのまま使えます。

書き込み後のモニタ(ホスト単独):

```bash
uv run --with pyserial python -m serial.tools.miniterm /dev/cu.usbmodemXXX 115200
```

### うまくいかないとき(AtomS3 特有)

- `rst:0x15 ... boot:0x20 DOWNLOAD(USB/UART0)` が出て画面が暗いまま
  → **モニタ open リセット罠**。`monitor_rts=0/monitor_dtr=0`(platformio.ini)で大抵回避。
  落ちたら **USB 抜き差し → コールドブート直後に monitor を開く**。ROM 待ちでも
  `pio remote run -t upload` は効くので書き込み直すと復帰
- `No serial data received` → USB ケーブル抜き差し
- `Association refused too many times` → テザリング相手が初回拒むことがある。リトライで接続

## Step 5: 実機で確認

**このSTEPでやること: ボードを設置して動かし、LCD の状態表示と LINE 通知が届くところまで確認します。**

1. 起動 → LCD に `BOOT` → `WIFI` → `RUN` の段階番号
2. 黄色 **`CALIB`** が約30秒(**途中で動かさない**。「何もない部屋」の基準を作る)
3. 正常監視: 黒背景の秒数カウント。上段 `W:on R1 mv:...`
4. 手を振る → 緑 **`MOVE`** → LINE 🏃 push(緑は動きが止んでも数秒残る。デバウンス仕様)
5. 完全に静止 → 緑が消えた瞬間に 0 再カウント:
   - **10秒** → 🚪 push + LCD 暗青 + `LEAVE`
   - **20秒** → 継続 push
   - **30秒** → 😴 push + LCD 紫 + `SLEEP`(以降 push なし。カウントは続く)
6. 再び動く → 緑 `MOVE` に復帰

検収ログ(CSI が流れているか):

```
[status] wifi=on  ready=1 motion=0 movement=0 idle_ms=1200 count=1 ...
[csi]    gen_pps=100 rx_pps=0 accepted_pps=100 admitted_pps=100 occ=1.00   ← GOOD
[line]   pushed pseudo_leave
```

`admitted_pps` が 0 付近なら CSI が流れていない(5GHz、WPA3 独占 AP など)。

## お題: しきい値を変える(10/20/30秒)

`include/secrets.h` を編集するだけで変えられます:

```c
#define PSEUDO_LEAVE_MS      10000UL  // 10秒で「動きがなくなりました」
#define PSEUDO_CONTINUED_MS  20000UL  // 20秒で「継続して動きがありません」
#define SLEEP_SUSPECTED_MS   30000UL  // 30秒で寝落ち(この後は push しない)
```

- **単位はミリ秒**。push 文言の「(○秒無動作・擬似)」も自動で追従
- `PSEUDO_LEAVE_MS < PSEUDO_CONTINUED_MS < SLEEP_SUSPECTED_MS` の順序だけ守る
- 変更は secrets.h 内なので git に載らない

その他の変更:

1. push 文言 → `src/line_push_client.cpp` の `makeText()`
2. LCDの色 → `src/display.cpp` の `color565(...)`
3. 自分のテストを追加 → `pio test -e native`(`lib/phantom_events/`)
4. BLE Beacon 送信(Phase 2)未実装 → `lib/line_beacon_frame/` はユニットテスト済みの土台

## トラブルシュート(AtomS3)

| 症状 | 対処 |
|---|---|
| `CALIB` から変わらない / `ready=0` のまま | CSI 未到達。`occ`/`admitted_pps` を見る(2.4GHz/802.11n 必須) |
| LCD に `NO-WIFI` | Wi-Fi 未接続。SSID/5GHz を疑う |
| push が来ない | シリアルの `[line]` を確認: 429=月200通上限 / 401=トークン失効 |
| 起動画面が暗いまま | モニタ開けでダウンロードモードに落ちた罠。USB再接続 or 再書き込み |

## 秘密情報の取り扱い

- `include/secrets.h` / `.env` は gitignore済み。`git status` で「untracked に載る」のが正しい状態
- 経緯・設計判断は [docs/blog.md](blog.md) を参照
