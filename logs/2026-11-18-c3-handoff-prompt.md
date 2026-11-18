<!-- 引き継ぎプロンプト(記録)。※この引き継ぎは当日中に同一セッションで消化済み:
     原因確定 → C3 実機検証済み → 調査コード片付け済み(handson-xiao-esp32c3.md 作成)。 -->

# 引き継ぎ: line-phantom-beacon C3 検証の続き (2026-11-18)

あなたは /workspace の line-phantom-beacon (PlatformIO/Arduino, M5Stack AtomS3 + 複数ボード対応) の
開発を引き継ぎます。まず AGENTS.md と ENVIRONMENT.md を読んでください。
直前のセッション(Fable 5.2)が ESP32-C3 起動ループの原因を突き止めました。

## 直前セッションの成果(必読)

読み物: `logs/2026-11-18-c3-investigation.md`(調査記録), `logs/2026-11-18-development-log.md`

**結論: C3 の起動ループは ESPectre ではなく「チップ上の古い 2nd-stage ブートローダー」が原因。
現行ブートローダー(IDF 5.5.5)+ huge_app パーティションを merged-firmware.bin で焼いた後、
C3 は ESPectre を含む本プロジェクトのファームで正常起動する**(実機確認済み: 1リセットのみ安定起動)。
`pio remote run -t upload` は bootloader/partitions を書かないため、
4MB ボードの初回書き込みは merged 直書きが必要(ATM Echo 版 handson と同じ手順)。

- チップ情報: XIAO ESP32-C3 = rev v0.3, 実フラッシュ 4MB
- ボード情報: C3 env は platformio.ini に再追加済み(`seeed_xiao_esp32c3`)。
  「⚠️ XIAO ESP32-C3 は未対応」のコメント(11行目付近)は**今は正しくないので更新が必要**

## 引き継ぎタスク(checklist)

1. **[未完] C3 実機の Wi-Fi/CSI 検証**: 本日はテザリング(SSIDは secrets.h 登録値)が見えず occ=0 で終了。
   テザリング ON の状態で `pio remote device monitor` にて
   `[wifi] connected` → `[csi] admitted_pps=〜90 occ=0.9` → 手を振って `[event] motion`
   → LINE push を確認(LINE 無料枠 200通/月は消費注意、429 なら既知)
2. **[未完] 調査コードの片付け**: 
   - `src/main.cpp`: setup() 直上/冒頭の `[probe] chip...` 印字ブロック(「C3 調査」コメント付き)を削除
   - `src/pad.c`: 調査用パッドファイル(PH_PAD_IROM がなければ inert)。削除してよい
   - `src/motion_sensor.cpp`: 「C3 調査用」の PHANTOM_LINK_ONLY ガードを削除
   - 削除後 `pio run -e seeed_xiao_esp32c3` と `pios run`/`pio test -e native`(17/17)を確認。
     **注意: 2026-11-18 の教訓により、ビルドキャッシュ詐欺があるため `touch src/*.cpp src/*.h` を
     変更後に必ず打ち、`strings .pio/build/<env>/firmware.elf` で期待した文字列の有無を確認してから焼くこと**
3. **[未完] platformio.ini のコメント更新**: C3 を「未対応」から
   「初回は merged-firmware.bin の直書きが必要(手順: docs/handson-atom-echo.md 参照)」に変更
4. **[未完] handson-atoms3.md / handson-atom-echo.md への反映**: 「4MB ボード(C3 含む)の
   初回はブートローダー+パーティションの書き換えが必要」を共通の注意として追記。
   handson-atoms3.md は現状 AtomS3 専用に書かれているが、XIAO C3/S3 でも同じ
   「merged 統合バイナリ」手順が有効
5. **[未完] README のハードウェア欄に C3 を復帰させる**(条件付き対応)
6. [任意] logs/2026-11-18-development-log.md の「C3 は ESPectre 問題で対応外」という記述を
   上書き修正(誤判定の訂正 + logs/2026-11-18-c3-investigation.md への誘導)
7. [任意] ESPectre の issue 報告は不要になった(ESPectre は無実)

## 知っておくべきトラップ(全て 2026-11-18 実測)

- `pio remote run -t upload` は bootloader(0x0)/partitions(0x8000)/boot_app0(0xe000) を焼かない。
  新チップ/別ファミリ初回は merged-firmware.bin をホストから
  `uvx esptool --chip esp32c3 --port <port> write-flash 0x0 <merged>` で直書き
- **ビルドと upload は同一フラグで 1 コマンドで**: `PLATFORMIO_BUILD_FLAGS="-DX=1" pio remote run -t upload -e <env>`
  (フラグなし upload が再コンパイルして別バイナリを焼く事故があった。Compressed バイト数で識別)
- ` PLATFORMIO_BUILD_FLAGS` 環境変数は有効(検証済み)。ただし後続の upload で env が消える点に注意
- モニタ open で Chip が DOWNLOAD モードに落ちる罠(rst:0x15 boot:0x4 waiting for download):
  回復は再焼き込み。USB 再挿し(冷起動)直後のモニタが最も安定
- agent は不定期に落ちる/クラウド接続が切れる → `pio remote agent start`(ホスト側)で回復。
  デバイスが agent から見えないときは USB 再挿し
- C3 のシリアルモニタは `pio remote device monitor --filter time` で秒数入りが解析に便利

## 現在のリポジトリ状態

- platformio.ini: C3 env(調査用コメント付き)あり・Echo/AtomS3/S3 env あり(default=atoms3)
- 未コミット変更: src/main.cpp(probe 印字), src/pad.c(調査用), src/motion_sensor.cpp(link-only ガード),
  platformio.ini(C3 env), logs/2026-11-18-c3-investigation.md, logs/(handoff 本体)
- 作業方針: AGENTS.md 参照(このファイルに書いた手順も踏襲)。コミット前に秘密情報チェック(git grep)。
  ボード上の C3 ファーム: **フルアプリ**(link-only なし。ESPectre begin/loop 有効。probe 印字が残存。
  Wi-Fi 未在のためテザリング待ちのスキャンリトライ中)。このチップの bootloader/partitions は
  現行版に更新済みなので、以後は `pio remote run -t upload -e seeed_xiao_esp32c3` だけで更新可
