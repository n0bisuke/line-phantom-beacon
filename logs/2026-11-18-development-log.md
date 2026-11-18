# 開発ログ 2026-11-18

## 複数ボード対応と XIAO ESP32-C3 の検証

### 目的
ハンズオンを AtomS3 専用→複数ボード対応にしたいという要望。まず XIAO ESP32-C3
(LCDなし・RISC-V・4MB と最も条件が厳しい)を試し、そこが通れば S3 系は派生とする方針。

### 実装した(そのまま残すもの)
- `platformio.ini`: 共通設定 `[env]` 化 + `seeed_xiao_esp32s3` env 追加。
  native 環境のため `[env:native]` では `framework =`(空)で上書きする必要があった
  ([env] 共通の framework=arduino が継承され native がボード要求で落ちるため)。
- `src/display.cpp`: `PHANTOM_NO_DISPLAYビルドフラグ` でヘッドレス動作(シリアルログのみ)。S3系env で使用
- マルチボードビルド自体は通る(AtomS3 54.7% / S3 39.6% / native 17/17)

### C3 検証結果 → 未対応と判断
1. ビルドは通る(huge_app.csv で app 約1.4MB)、書き込みも OK
2. しかし実機は **起動ループ**(`rst:0x3 RTC_SW_SYS_RST` を ~0.25s 周期で無限繰り返し、
   アプリのログが一切出ない。`E (214) esp_core_dump...` より前 = IDF初期化前に死亡)
3. 切り分け(センサー初期化スキップフラグのA/B)で判明:
   - **ESPectre のランタイムTUsがリンクされたアプリのみ死亡**(executed code の有無と無関係)
   - lazy 構築(グローバルコンストラクタ排除)でも死亡
   - PING トラフィック(WIFI_RAW無し)でも死亡
   - skip ビルド(センサー未リンク)は正常起動し Wi-Fi 接続もできた
4. 「実行されないコードがリンクされるだけで死ぬ」ため ESPectre ライブラリ側の問題
   (C3 向けコード生成・配置 etc.)と判断。本リポジトリ側の対処は不可能と結論
5. 決着: **XIAO ESP32-C3 は対応外**。platformio.ini から env 削除し、既知問題を記載
   (PHANTOM_SKIP_SENSOR を活かした「Wi-Fi のみ動く」C3 ビルドを一時的に焼いたが、
   クリーン化に伴い env は削除)

### 検証方法の改善(この過程で確立)
- **esp_rfc2217_server トンネル**: ホストで `uvx --from esptool esp_rfc2217_server -v -p 4000
  /dev/cu.usbmodem101` → コンテナから `rfc2217://host.docker.internal:4000`。
  モニタは `?ign_set_control` 付き(さもないと DTR 応答待ちでタイムアウト)、
  マウントし続けるためモニタ接続ごとのリセット罠を回避できる。
  ただし**アップロードは DTR/RTS のタイミングがテロネルート越しに合わず不可**
  (USJ チップのダウンロードモード遷移で失敗: Wrong boot mode 0x8)。
  書き込みは従来どおり `pio remote`(agent)を使う
- **ビルドキャッシュの罠**: ソースを cp で復元した時に PlatformIO が変更を検出できず、
  古いバイナリを焼いて解析を迷わせた。ソース復元後は `touch` か clean 推奨
- ボードリセット(抜き差し相当)の完全自動化は現環境では不可能
  (エミュレータリセットで USJ のダウンロードモードに入れない)

### クリーン化(ユーザー指示)
診断用コード([main] setup enter デバッグ行、PHANTOM_SKIP_SENSOR / PHANTOM_WIFI_RAW
フラグ、ESPectre 遅延構築)を全て除去し、AtomS3 実機検証済み構成に戻した。
AtomS3 ビルドは検証済み版と同一サイズ(Flash 1,826,983 bytes / 54.7%)で一致を確認。
残すのは正当な機能: `PHANTOM_NO_DISPLAY`(S3系)のみ。C3 env は削除。

### 本日のまとめ(2026-11-18 の成果と状態)

**動いたもの(実機検証済み)**
- **M5Stack ATOM Echo で line-phantom-beacon が動作**: Wi-Fi 接続(テザリング: SSID は secrets.h 登録値)、
  CSI(wifi_raw, occ 0.89-0.99 / admitted 90-99pps)、キャリブレーション完了、疑似イベント状態機、
  起動ステージ表示。ATOM Matrix/Lite も同一 env(m5stack-atom)で動くはず(未実機確認)
- **Echo 固有の実装**: 内蔵 SK6812 LED(GPIO27)による状態色表示(起動=白/キャリブ=青/監視=緑/
  動き=赤/leave=シアン/寝落ち=紫/NO-WIFI=オレンジ)、push の端末ラベル env 上書き
  (`PHANTOM_DEVICE_ID=atom-echo`)
- **4MB・クラシックESP32 ボードへの書き込みフロー確立**:
  `pio remote upload` は bootloader(0x1000)/partitions(0x8000) を焼かないため、
  初回は `esptool merge-bin` で merged-firmware.bin を作りホストから
  `uvx esptool write-flash 0x0`(ボーレート指定なし=115200)で直書き。
  一度焼けば以後 `pio remote run -t upload` で更新可(アプリ領域のみ)

**未解決 / 対応外 / 未確認**
- Echo のビープ音: **出ない**(既知問題)。スピーカーは Class-D(DAC/GPIO25駆動)のため
  LEDC PWM は鳴らず、DAC + esp_timer 方式に変更したが実機で音未確認。LED 他は正常
- **XIAO ESP32-C3 = 対応外**(前述の ESPectre バグ)
- XIAO ESP32-S3 Plus: USB が列挙されず未検証(ケーブル/基板の要因疑い、復帰後に再試行)
- LINE push: 無料プラン月200通の上限に到達し **429**。毎月1日リセット(handson に注意書き済み)
- ESPectre への C3 バグ報告(未実施。最小再現コードを整理して報告が望ましい)

**ドキュメント更新**(資料はボード別に分割: handson.md は廃止)
- `docs/handson-atoms3.md` / `docs/handson-atom-echo.md`: ボード別ハンズオン(Codespaces・ローカル完結ルート両対応)。
  ホスト事前準備(pio + uv)、LINE 200通注意、しきい値変更のお題(secrets.h 編集)、ボード表
- 検収基準・トラブルシュートは各ボード資料へ集約

### 備考: 診断で重要だった事実
- `rst:0x3 (RTC_SW_SYS_RST)` = esp_restart。ペニック印刷すら出ない早期死
- `E (214) esp_core_dump_flash...` の出現有無が「アプリがIDF初期化に到達したか」の
  有力な判定材料(214ms 時点)
- C3 では Arduino グローバルコンストラクタは main 前に走る(`_GLOBAL__sub_I_*` は
  init_array で実行位置を特定できた)

## 2026-11-18(日中追記): C3 起動ループの原因確定(誤判定の訂正)

 単純に誤判断としてまとめ直した。判定は「ESPectre 由来」としたが**誤り**。
- 正しい原因は「**チップ上に残っていた古い 2nd-stage bootloader**」(pio remote upload は
  bootloader/partitions を書かないため更新されないまま)。
  古い bootloader は IROM≈1MB(0xF0000=64KBページ15枚)を越えるアプリをマップできず、
  ログ無しの起動ループ(rst:0x3)を出す。rev v0.3 + flash 4MB(XMC) で実測。
- 対処: 初回のみ merged-firmware.bin で bootloader+partitions+app を丸ごと焼く。
  以後は pio remote のアプリ書き込みのみでOK。
- その後の実機検証: C3(rev v0.3)で Wi-Fi 接続(2AP切替)/CSI occ≈1.0/動き検知/LINE push まで**全て確認**。
- 調査詳細: logs/2026-11-18-c3-investigation.md
- ドキュメント: docs/handson-xiao-esp32c3.md を新規作成(ボード別資料の3冊目)
- 「C3 未対応」の記述は platformio.ini / README / 本ログの記載を訂正済み


## 2026-11-18(夕方追記): Grafana Cloud テレメトリ(オプション機能)

- LINE の月200通制限とは別に、**全イベント・presence・稼働状態を Grafana Cloud(無料)の Loki に
  直接 HTTPS POST** する機能を追加(`src/telemetry_client`)。push 例: イベント行+presence 遷移を
  1リクエスト、状態 heartbeat は 5 分ごと。
- TLS は DigiCert Global G2 系の CA 連鎖(`src/telemetry_root_ca.h`)で検証(leaf は含めない)。
- secrets.h に `TELEMETRY_LOKI_URL/USER/TOKEN` を設定しなければ完全に無効。
- リアルタイム性: 数秒〜10秒のラグ。無料枠は「3ユーザ/50GB月(ログ)/保持10日」で本用途は余裕。
- README にセットアップ手順を記載。全 env ビルド+ネイティブテスト 17/17 通過。


## 2026-11-18(夜追記): XIAO ESP32-C6 対応(DNS トラフィック)

- XIAO ESP32-C6 を新対応。**ESPectre 3.2.0 は C6 で WIFI_RAW をライブラリ側で拒否する**
  (runtime_config_utils.cpp の runtime_traffic_generator_mode_supported)ため、
  `PHANTOM_TRAFFIC_DNS` フラグで DNS クエリによるトラフィック生成を追加
  (motion_sensor.cpp: PHANTOM_TRAFFIC_DNS/PING/WIFI_RAW を env で選択可能に)。C6 env では DNS 固定
- C6 実機確認: `traffic=dns` で fault なし、**CSI occ 0.88-0.91 / admitted 88-91pps**(DNS でも CSI が流れる)。
  Wi-Fi 接続/キャリブレーション完了/リセット1回。LINE push(ラベル xiao-c6)はモニタ罠でシリアル未確認だが
  line_push_client は全ボード同一実装
- 4MB ボードの初回書き込み: **ローカル `pio run -t upload` は bootloader+パーティション込みで焼ける**
  (FLASH_EXTRA_IMAGES は pio remote のみ欠落)。Codespaces 経由のみ初回 merged 直書きが必要
- docs/handson-xiao-esp32c6.md を新規作成(ボード別資料4冊目)

- docs/readme.md を新規作成: 4 ボード(AtomS3/Echo/C3/C6)実機検証の注意点・落とし穴の実記録。
  README を全面書き換え(4 ボード対応・書き込みの要点・LED/トラフィックモード仕様・検証状況表)。
  C6 は LINE push 到達をユーザーが LINE アプリで確認済み → 4 ボード対応完了。
