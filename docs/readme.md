# ボード別 検証の注意点・落とし穴(実記録)

4 ボード(AtomS3 / ATOM Echo / XIAO ESP32-C3 / XIAO ESP32-C6)を 2026-11-12〜11-18 に実機検証した際に
**実際に起きたこと**を、ボード別+共通に分けてまとめたもの。ハンズオン前に一読推奨。
(全て platformio-espressif32 8.0.0 / Arduino-ESP32 core 3.3.12 での実績なので、バージョンを上げたら再確認)

## 共通(どのボードでも効いた教訓)

- **モニタ open はリセット罠**: ボード稼働中に `pio ... device monitor` を開くと
  `rst:0x15 ... boot:0xX (DOWNLOAD/USB...)` の ROM ダウンロードモードに落ちる(全ボード経験)。
  対処: **USB 抜き差し(冷起動)直後にモニタを開く**のが最も安定。落ちたら再書き込みで復帰
  (`platformio.ini` に `monitor_rts=0 / monitor_dtr=0` 済みだが、タイミングで発動する)
- **連発する場合は RFC2217 トンネル**: ホストで
  `uvx --from esptool esp_rfc2217_server -v -p 4000 /dev/cu.usbmodemXXX` を常駐 →
  コンテナ/Codespaces から `rfc2217://host.docker.internal:4000?ign_set_control`。
  ポートが保持されるので open 毎のリセットが起きない(エコーC3/C6 で復回成功)
- **`PLATFORMIO_BUILD_FLAGS` 付きビルドの後、フラグなしで upload すると再コンパイルが走り
  別バイナリが焼かれる**: ビルドと書き込みは**同じフラグの 1 コマンドで**。
  識別は upload 出力の `Compressed <bytes>` で
- **ソース差し替え(cp)後のビルドは疑ってかかる**: PlatformIO の依存キャッシュが再コンパイルを
  検出しないことがあった。焼く前に `strings .pio/build/<env>/firmware.elf | grep <期待文字列>` で
  中身を確認する癖を付ける
- **4MB ボードの初回書き込み**: bootloader+パーティション+アプリをまとめて焼く必要がある。
  **ローカルの `pio run -t upload` は全部書く**(推奨)。Codespaces+`pio remote` は**アプリ領域のみ**
  書くため初回だけ統合バイナリ直書きが必要(手順は各 handson)。**他プロジェクトで使ったチップは
  古い bootloader が残っている**ので必須
- トリビア: Line チャネル側の push は `429` で止まる(月200通・毎月1日リセット)。Serial には
  `[line] push ... failed: 429` と出る

## AtomS3(m5stack-atoms3)

- **LCD 表示が主の落とし穴**: `m5stack-atoms3` ボード定義には `ARDUINO_USB_CDC_ON_BOOT` が無い。
  ケーブルを挿して PC にログが一切出ない場合はこれ(platformio.ini で指定済み)
- モニタ open 時のリセット罠はこのボードで最初に発見(0.25 秒周期の
  `rst:0x15 boot:0x20 DOWNLOAD` 連発)。`monitor_rts=0/monitor_dtr=0` で大抵回避
- ESPectre のキャリブレーションは**約 27〜35 秒**。この間に動かすと再キャリブレーション扱いになる
- CSI は既定の PING(ICMP)ではテザリング(iPhone)相手に通らない → **WIFI_RAW**(null data+ACK)必須
- このボードは remote 書き込みで何度でも再書き込みできた(8MB なので bootloader 問題に遭遇せず)

## ATOM Echo(m5stack-atom)

- **4MB → 初回は統合バイナリ直書きが必須**。`pio remote` でアプリだけ焼いても
  `Factory app partition is invalid` と出て起動ループになる(工場出荷時に残っている「1MB アプリ」
  のパーティション定義が邪魔)。手順: `esptool --chip esp32 merge-bin` → ホストで
  `uvx esptool --chip esp32 --port /dev/cu.usbserial-... write-flash 0x0 merged-firmware.bin`
- **UV 写真のポートは `cu.usbserial-…` 形式**(crystal ESP32 は USB-UART ブリッジなので)。
  `--baud 921600` 付きの esptool は `Unable to verify flash chip connection` が出るので
  **既定 115200**(約 75 秒)で
- **オンボード RGB(SK6812, GPIO27)** は `neopixelWrite` で安定表示(動作確認済み)
- **スピーカーは Class-D アンプを DAC(GPIO25)で駆動**: LEDC PWM では鳴らない。
  DAC+esp_timer 方式に変更したが**実機で音を出すまでには至っていない(未解決)**。
  鳴らない場合も他の動作(LED・push・検知)は全て正常
- Matrix/Lite にも同じ env で対応(Echo と同ファミリ)。Matrix は 5×5 RGB マトリクスがあるが
  未実装(今後表示に使える)

## XIAO ESP32-C3

- **起動ループの原因は「チップ上に残った古い bootloader」**(ESPectre を詐称した判定は誤り)。
  症状: `rst:0x3` を ~0.25 秒周期で連発し、**アプリ(ESPectre)のコードは実行されない**ため
  シリアル出力が一切無い。ロボットチップ: **rev v0.3 / 4MB フラッシュ(XMC)**
- 判別実験: 同一コード+0x11400 バイトの IROM パッド(IROM が絶対フラッシュ 1MB を越える)
  → 起動ループ化。**bootloader を現行版に置き換えると同一バイナリが起動**
- **対処は初回の merged 直書き**(bootloader+partitions+app)。以後は `pio remote` で更新可。
  調査記録: `logs/2026-11-18-c3-investigation.md`
- こうした「**他プロジェクトで使ったボードに新しい大型アプリを焼く**」というパターンは
  手持ちのどの ESP32 でも発生し得る(原因は ESP32-C3 の bootloader で特に頻出)
  なお正常動作時は CSI occ≈1.00/100pps で Wi-Fi 2AP 切替込みで安定して検知できた

## XIAO ESP32-C6

- **ESPectre 3.2.0 は ESP32-C6 での WIFI_RAW をライブラリ側で拒否**
  (`runtime_traffic_generator_mode_supported` を参照)。`s_sensor.begin()` が
  `[espectre] fault: invalid traffic generator mode` で失敗し検知が無効になる
- **対処済み**: `PHANTOM_TRAFFIC_DNS` フラグ(C6 env では既定)。DNS クエリで CSI 駆動し
  CSI occ≈0.88-0.91 / admitted 88-91pps を確認(初回)
- 4MB ボードなので bootloader/パーティションを初回に焼く必要あり(ローカル `pio run -t upload` は
  一発で入った)。bootloader が更新されていない他ボードでは上記 C3 の起動ループと同症状になり得る
- 写真のオンボード LED は **GPIO15・アクティブ LOW**(LOW で点灯)、単色の
  青色(Seeed wiki: https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)。

## XIAO ESP32-S3(+Plus/Sense)

- **未検証**。実機(XIAO ESP32-S3 Plus)が PC に USB で列挙しない問題(`cu.usbmodem…` が上がらない)
  で検証が止まっている。ケーブルのデータライン・コネクタの向き・BOOT+RESET リセットも試みたが
  失敗(env `seeed_xiao_esp32s3` はビルド済み。8MB なので bootloader 問題は軽微のはず)
- 復帰後は: `pio run -t upload -e seeed_xiao_esp32s3` → `pio device monitor` で検収
  (handson-atoms3.md の LCD なし版の手順と同じ)
