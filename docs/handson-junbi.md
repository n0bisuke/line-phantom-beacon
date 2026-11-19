# 共通準備ガイド(handson-junbi)

ボード共通のハンズオンを始める前に揃える**アカウント・キー・知識**の準備ガイドです。
作業時間: 約30分(アカウント作成を含む)。
ボードごとのビルド/書き込み手順は各 handson 資料を参照:
[AtomS3](handson-atoms3.md) / [ATOM Echo](handson-atom-echo.md) / [XIAO C3](handson-xiao-esp32c3.md) / [XIAO C6](handson-xiao-esp32c6.md)

## 0. 持ち物チェックリスト

| # | 項目 | メモ |
|---|---|---|
| 1 | ESP32 ボード(手持ち) | AtomS3 / ATOM Echo / XIAO C3 / XIAO C6 のどれか |
| 2 | USB-C **データ通信用**ケーブル | 充電専用ケーブルは書き込み不可の典型原因 |
| 3 | **2.4GHz Wi-Fi**(スマホのテザリング可) | 5GHz のみでは CSI が取れない |
| 4 | GitHub アカウント | Codespaces を使う場合のみ必須 |
| 5 | LINE アカウント | 下記 STEP 1 で Bot 化する |

## STEP 1: LINE Bot(公式アカウント)の作成と 2つのキー

詳細スクリーンショット付き: [Zenn — LINEボット設定チュートリアル](https://zenn.dev/protoout/articles/16-line-bot-setup)

このプロジェクトで必要なのは **チャネルアクセストークン(長期)** と **自分のユーザーID** の2つだけ。
(Webhook 設定・チャネルシークレットは push 方式では使いません)

1. **LINE Developers コンソールにログイン** — [https://developers.line.biz/](https://developers.line.biz/)。
   LINE アカウント(QR)でログイン。初回のみ開発者情報(氏名/メール)を登録
2. **プロバイダー作成** — 「新規プロバイダー作成」ニックネームでOK(参加者が見る場合があるため本名推奨しない)
3. **公式アカウント(MSGチャネル)作成** — プロバイダーから「Messaging API」を選択し、公式アカウントを
   作る(初回は SMS 認証あり)。アカウント名に「LINE」を含めるとエラーになるので注意
4. **Messaging API を有効化** — LINE Official Account Manager (manager.line.biz) → 設定 →
   **Messaging API** → 「Messaging API を利用する」→ 作成済みプロバイダーを選択して確定
5. **チャネルアクセストークン(長期)を発行** — LINE Developers コンソール → チャネル →
   **Messaging API 設定**タブの最下部「チャネルアクセストークン(長期)」→ 発行 → 文字列をコピー
   - ⚠️ **ブラウザの自動翻訳をオフにしてからアクセス**(翻訳で文字列が壊れる事故が多発)
   - 隣の「チャネルシークレット」は本プロジェクトでは不要(押さない)
6. **ユーザーIDを取得** — 同じ **Messaging API 設定**タブの「**Your user ID**」(`U` で始まる34字)をコピー
7. **Bot を自分の LINE に友だち追加** — 同タブの QR コードを LINE で読み込む
   - これをしないと push が届かない(公式アカウントは友だち追加が必要)
8. **しきい値確認** — 友だち追加後、1通pushできるか手元で確認しておくと安心。

> ⚠️ LINE 無料プランの push 上限は **月200通/チャネル**。検証中に消費し、超えると HTTP 429
> (毎月1日リセット)。詳しくは [handsons-ouyou.md](handsons-ouyou.md)。

**secrets.h への記入**:

```c
#define LINE_CHANNEL_TOKEN "<STEP1-5 で発行したトークン>"
#define LINE_USER_ID       "<STEP1-6 で確認した U..."
```

## STEP 2: LINE Beacon の HWID 登録(Phase 2 前段階。本ハンズオンでは任意)

LINE Simple Beacon は「ESP32 がビーコン発信 → スマホ付近検知」形式(本プロジェクトの Phase 2)。
CSI 動き検知の検証には不要ですが、先に発行しておくと後で詰まりません。

1. [https://manager.line.biz/beacon/register](https://manager.line.biz/beacon/register) にアクセス
2. ビーコン種別「LINE Simple Beacon」を選び登録
3. 発行された **HWID(16進10文字/5バイト)** を `include/secrets.h` の `LINE_BEACON_HWID_HEX` に記録
   (Device Message は未使用なら空文字のまま)

## STEP 3: ESPectre(CSI 動き検知)の背景知識

### 原理をひとことで

- Wi-Fi(802.11n)のパケットには、空間の伝搬状況を表す **CSI(Channel State Information)** が含まれる
- 人が動くと**反射波・回折波の経路が変わり、サブキャリア毎の振幅と位相が乱れる**
- ESPectre はその乱れを数学(フィルタ+特徴抽出+閾値判定)で解析し「動きあり/なし」を出す
  カメラ・マイク不要(プライバシーに比較的やさしい)で、**壁越しの動きも拾い得る**
- 本プロジェクトは ESPectre を PlatformIO(Arduino framework)から直接制御して
  擬似イベント→LINE push に接続している

### 読み物リンク

| 読み物 | 内容 |
|---|---|
| [Gigazine — Wi-Fi をモーションセンサー化](https://gigazine.net/news/20251118-turned-wi-fi-motion-sensor/) | ESPectre の開発経緯・仕組み(数式のみで95%精度/50ms未満)、限界(人とペットの区別、人数推定不可)、プライバシー注意 |
| [Korben — ESPectre カメラなしモーション検知](https://korben.info/ja/espectre-wifi-motion-detection-sans-camera.html) | 同じく概要解説。Home Assistant 統合や活用例(見守り/空室の空調オフ等) |
| [Seeed Wiki — XIAO シリーズ × ESPectre](https://wiki.seeedstudio.com/ja/xiao-esp32--series-espresense/) | **ESPHome/Home Assistant 経由の別ルート**。設置ガイド(ルーターから3〜8m・高さ1〜1.5m・金属回避)も載っている |
| ESPectre 官方 | [GitHub](https://github.com/francescopace/ESPectre) / [公式サイト](https://espectre.dev/) |

### 本プロジェクトと Seeed Wiki ルートの違い

| | 本プロジェクト(PlatformIO 直) | Seeed Wiki(ESPHome + Home Assistant) |
|---|---|---|
| 通知 | **LINE push + Grafana** | Home Assistant バイナリセンサー |
| 設定 | `secrets.h`(C++ マクロ) | YAML + ビルド |
| キャリブレーション | Lightweight: 起動時(約30秒静止)/ High-Accuracy 切替可 | `Calibrate` ボタン(同じく無人状態で) |
| 向いている人 | LINE 通知・ミニアプリ的体験が欲しい人 | Home Assistant ユーザー |

※両者は共存できる ESPectre の同じコア。本プロジェクトは LINE 主眼で簡素化している。

### 設置の勘所

- ルーターと ESP32 の距離 **3〜8m / 高さ 1〜1.5m**、金属・大きな水槽を挟まない
- 検知領域は「直線」ではなく**電波が届く空間全体**(隣室や窓際の動きにも反応し得る。逆に同室内でも
  死角がある)→ 詳細は [handsons-ouyou.md](handsons-ouyou.md)
- 無人時に検知が出たら: まず occ / pps / RSSI を確認(センサー感度より先に Wi-Fi の健康を疑う)

## STEP 4: 開発環境の準備

**ローカル PC ルート(推奨・初回 bootloader 込みで簡単)**:

```bash
pip install -U platformio     # PlatformIO CLI
```

あとはボード別 handson 資料の通り: `pio run -e <env>` → `pio run -t upload -e <env>` → `pio device monitor`。
(初回ビルドで ESP32 ツールチェーン約3GBのダウンロード、10分+)

**Codespaces ルート(USB に触れる PC が参加者のものと別の場合)**:
リポジトリを fork → Codespaces を開く → ホスト PC に `pio remote agent start` →
コンテナから `pio remote run -t upload`。4MB ボードの初回のみ統合バイナリのホスト直書き
(uv / uvx esptool)が必要 — 各 handson 資料参照。

## 秘密情報まとめ(secrets.h に書くもの一覧)

| 定義 | 取り方 | 必須 |
|---|---|---|
| `WIFI_CREDENTIALS` | 自分の Wi-Fi(2.4GHz)。テザリング可 | ✅ |
| `LINE_CHANNEL_TOKEN` | STEP 1-5 | ✅ |
| `LINE_USER_ID` | STEP 1-6 | ✅ |
| `DEVICE_ID` | 任意(未指定なら env のラベル: xiao-c3 など) | 任意 |
| `PSEUDO_LEAVE_MS` 等 | 好みのしきい値(10/20/30秒が既定) | 任意 |
| `TELEMETRY_TOKEN` 等 | Grafana Cloud を使う場合のみ(SECTION README参照) | 任意 |
| `LINE_BEACON_HWID_HEX` | STEP 2(Phase 2 だけ) | 任意 |

> どれも `include/secrets.h`(gitignore済み)に記入。**コミットしない**。
> トークンを誤って push したら LINE Developers コンソールで即ローテーション。
