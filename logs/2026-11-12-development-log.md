# 開発ログ (2026-11-12 / Phase 1〜の状況)

このファイルはセッション単位の作業記録。更新のたびに追記する。

## 今日の成果

### ビルド & テスト(全てコンテナ内で完結)
- `pio run` SUCCESS:RAM 15.4%(50,624B)/ Flash 49.4%(1,650,303B)。M5Unified(LCD表示)込み
- ネイティブテスト **16/16 全パス**(`pio test -e native`)
  - `test_line_beacon` 7テスト:LINE Simple Beacon 公式バイト列と完全一致
  - `test_events` 9テスト:疑似イベント状態遷移(30秒/3分境界、重複抑止、wrap耐性)
- テストバグから設計バグを1件発見・修正:sleep_suspected発火後のtickで pseudo_leave が後発火する → sleep発火時にleaveを出済み扱いにする修正を `lib/phantom_events` に適用

### 実機書き込み環境(未検証の「書けない」を解決まで持っていった)
1. **AtomS3ボード定義には `ARDUINO_USB_CDC_ON_BOOT` がない**ため、素の `Serial` はGPIO1のUARTに出てPCには何も見えない — これが「書き込み後何も起こらない」の理由。
   対策: `platformio.ini` の `build_flags = -DARDUINO_USB_CDC_ON_BOOT=1` を追加
2. 書き込み方式を試行錯誤した末、**`pio remote` 方式が最良**:
   - ホスト(macOS): `pio account login`(1回)→ `pio remote agent start`(常駐)
   - コンテナ: 環境変数 `PLATFORMIO_AUTH_TOKEN`(ソース確認済み、`account/client.py`が優先読み取り)でAPIトークンログイン → `pio remote run -t upload` / `pio remote device monitor`
   - 認証トークンは `.env` の `PLATFORMIO_AUTH_TOKEN=...` に置く(gitignore済み)
   - **リモートアップロード成功**:コンテナでビルドした成果物がクラウド経由でagentのボードに書けた。ホストにespressif32ツールチェーンは不要(→ ホストの ~/.platformio/packages は不要)
3. 崩した代替パス(参考): コンテナでビルド → `esptool merge_bin` でbootloader+partitions+appを統合 → ホストで `uvx esptool write-flash 0x0 merged-firmware.bin`。OrbStack USB(`orb usb attach/detach` + 抜き差し)でのポート消失にも対処手順あり
4. LCD表示: `m5stack/M5Unified@0.3.0` を追加。ステータス4行表示 + 現在は**起動診断モード**(S0 BOOT / S1 WIFI / S2 RUN と段階表示)

### 現在未解決(作業中)
- 診断ファーム書き込み直後、**LCDが真っ黒**な問題を調査中。シリアルが読めていなかったので、起動段階をLCDに表示するS0/S1/S2方式を追加済み。次回: どこで止まるかで原因特定(M5.beginの初期化順/ESPectre begin内のブロック/クラッシュループ)

## 学び(環境)

- PlatformIOのネイティブテストスイートは **`test_*` で始まるディレクトリ**のときしか収集されない(`test/<name>/` のみ有効、`test/events/` は無視)
- OrbStackの `orb usb attach` はmacOSからデバイスを奪う。`detach`+**USB抜き差し**で戻る
- `!` によるClaude Codeからの対話式コマンド(stdin)は使えない(abortする)→ トークン等は `.env` 経由
- PlatformIO 6.2.0: `pio account login --token` は無い。**環境変数 `PLATFORMIO_AUTH_TOKEN` を使う**

## 次のタスク

1. LCDが真っ黒の原因特定(診断ファームでステージ確認)
2. Phase 1 実機検証の完了(ready=1 → motion 検知)
3. git 初回コミット(秘密情報が含まれないことの最終確認)
4. Phase 2: BLE Advertising発信(Bluedroid標準BLE方針確認済み)
5. Phase 5: Node.js中継サーバー(Node.jsはコンテナに無いので別コンテナ/ホストで)
