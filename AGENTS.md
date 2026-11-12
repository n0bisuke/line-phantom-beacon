# 作業ルール (line-phantom-beacon)

## 目的と優先順位

<!-- TODO: 何を作るか、使う部品(センサー・モジュール等)と配線を書く -->
このリポジトリは PlatformIO (`arduino` framework) で M5Stack AtomS3 向けのファームウェアを開発します。まず実機で確実に動く小さなものを完成させてください。

作業開始時に `platformio.ini`、`src/`、コンテナの環境説明(`/opt/dev-env/ENVIRONMENT.md`)を読んでください。既存の構成・命名を尊重し、依頼された変更に必要な範囲だけ編集します。

## ハードウェアの前提

<!-- TODO: ボード、ピン割り当て、電源、接続する部品を書く -->
- ボード: M5Stack AtomS3(`board = m5stack-atoms3`)
- ピン割り当て: (ここに書く)

## コード方針

- C++ (Arduino) または C (ESP-IDF)。1ファイル約400行以下を目安に、責務ごとに分割する。
- ピン番号・定数は1か所(例: `src/config.h`)にまとめる。
- `delay()` に頼った長いブロッキングを避け、`millis()` ベースで処理する。
- Wi-Fi のSSID・パスワード、APIキー等の秘密情報をソースにハードコードしない(`include/secrets.h` 等に置き `.gitignore` する、またはビルドフラグで渡す)。
- ライブラリを追加するときは `platformio.ini` の `lib_deps` にバージョン付きで書く。

## 開発環境の制約

- ビルドは Docker コンテナ内の `pio run` で行う。環境定義はこのリポジトリには無く、共通リポジトリ `shared-dev-environments/pio/` で管理している。
- コンテナから USB シリアルに直接アクセスできない。書き込み・シリアルモニタはユーザーがホストで行う(手順は README)。書き込めたように報告しない。
- ホストのディレクトリを PlatformIO のキャッシュとして bind mount しない。ツールチェーンは共通Volumeを使う。

## 変更と検証

1. 変更前に関連するファイルと `platformio.ini` を確認する。
2. 小さい単位で実装し、`pio run` でビルドが通ることを確認する。
3. 実機での動作(配線・センサー値・通信)はビルドだけで確認済みとみなさず、ユーザーに確認手順を示して「未確認」と報告する。
4. 作業後は変更ファイル、ビルド結果、フラッシュ/RAM使用量、未確認の項目を簡潔に報告する。

## 公開Gitリポジトリの確認

- 公開リポジトリとして扱う。コミット前に `git status` と `git ls-files` で秘密情報(Wi-Fi情報、APIキー、`.env`)が追跡対象に入っていないか確認する。
- シリアルログにも個人情報・ネットワーク情報が含まれ得るため、原則コミットしない。

ユーザーが依頼していない依存追加、ネットワーク送信、データ削除、Docker volume の削除は行わない。既存ファイルを消したり移動したりする前に、その必要性を説明してユーザーに確認する。

## 現在の開発フロー (2026-11-12 確立済み)

### ビルド・テスト・書き込み
1. **ビルド・テストはコンテナで行う**
   - ビルド: `pio run`。成果物は `.pio/build/m5stack-atoms3/`(ホストから見える)
   - ユニットテスト: `pio test -e native`(テストは `test/test_<名前>/test_*.cpp` に置く)
2. **書き込み・モニタは `pio remote` 経由**で行う(3GBのホストツールチェーンは不要)
   - ホスト(macOS): `pio account login`(初回のみ)→ `pio remote agent start`(常駐)
   - コンテナ: `.env` の `PLATFORMIO_AUTH_TOKEN` を環境変数にして実行:
     ```bash
     export PLATFORMIO_AUTH_TOKEN=$(sed -n 's/^PLATFORMIO_AUTH_TOKEN=//p' .env)
     pio remote run -t upload          # 書き込み
     pio remote device monitor         # モニタ
     ```
   - トークンは platformio.org/account/tokens で発行。**Gitにコミットしない**
3. **緊急時の代替パス**(remoteが使えない場合):
   - コンテナで `esptool merge_bin` により bootloader+partitions+appを統合した `merged-firmware.bin` を作る → ホストで `uvx esptool write-flash 0x0 <bin>` で直書き
   - OrbStackの `orb usb attach/detach` は macOS からデバイスを奪う/返す。detach後はUSB抜き差しが必要なことがある

### ファームウェアの注意
- AtomS3 (`m5stack-atoms3`) ボード定義には `ARDUINO_USB_CDC_ON_BOOT` がない。PC側でシリアルログを見るには `build_flags = -DARDUINO_USB_CDC_ON_BOOT=1` が**必須**(platformio.iniに追記済み)
- LCD表示は `m5stack/M5Unified@0.3.0` を使用

### 作業記録
- セッション単位の作業記録は `logs/` ディレクトリに日付入りで残す(例: `logs/2026-11-12-development-log.md`)
