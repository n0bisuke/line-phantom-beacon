# C3 起動ループ 原因調査記録 (2026-11-18, Fable 5.2)

## 結論(先に)

**根本原因は ESPectre ではない。** XIAO ESP32-C3 のチップ上に残っていた**古い
2nd-stage ブートローダー**(過去の別プロジェクト書き込み時に焼かれたもの)が、
**IROM セグメント長が約 0xF0000(960KB ≈ 絶対フラッシュ 1MB 境界)を越えるアプリを
マップできず、エラー出力なしで esp_restart(=rst:0x3, ~0.25s 周期)していた**。

- `pio remote run -t upload` は **bootloader(0x0) / partitions(0x8000) を書かない**
  (アプリ領域 0x10000 のみ。2026-11-18 に Echo で実証済み)。したがって C3 の
  bootloader+パーティションは一度も更新されず、チップには前回プロジェクト由来の
  古いものが残る
- 現行(IDF v5.5.5)の bootloader + huge_app パーティションを merged-firmware.bin で
  丸ごと焼いたところ、**IROM 0xFF1F8(>0xF0000)のアプリが 1 リセットもなく正常起動**。
  ESPectre を含む本プロジェクトのファームは、正しいブートローダーが入っていれば
  **C3 でも動作する**
- 対応手順(4MB ボードの初回書き込み): **初回のみ merged-firmware.bin をホストから
  esptool write-flash 0x0 で焼く**(ATM Echo 版 handson と同じ手順)。以後は
  `pio remote run -t upload` で OK

## 補助事実

- チップ: **ESP32-C3 rev v0.3**(`esp_chip_info().revision == 0x0003`, cores=1)
- 実フラッシュ: **4096KB**(`esp_flash_get_physical_size` = 4MB — 容量は正常)
- チップ上の bootloader のみが原因。アプリ側コード・ESPectre・トラフィック生成
  モード(PING/WIFI_RAW)・グローバルコンストラクタの有無は**すべて無関係**
- 2026-11-18 の「C3 は ESPectre 3.2.0 × C3 ビルドでリンクするだけで死ぬ」という
  判定は、**誤った A/B 比較(後述)による誤結論**だった

## 有効だった証拠(本日のクリーン データ)

同一コード(+0x11400 バイトの IROM 内パッドのみ差分)の対比:

| ビルド | IROM 長 | 結果 |
|---|---|---|
| c3_probe (link-only, pad 無し) | 0xEDD68 | 正常起動(Wi-Fi 接続・status ループ) |
| c3_pad_irom (link-only + IROM+0x11400) | 0xFF1F8 | 起動ループ(238 リセット) |
| 同 pad ビルドを **merged-firmware で焼く** | 0xFF1F8(同一) | **正常起動** |

- 理論面: C3 の DROM 開始仮想アドレスはリンカが「IROM の終端 + 段差」に置くため
  「IROM 長 ≥ 約0xF0000」と「DROM vaddr ≥ 0x3c100000(ウィンドウ+1MB)」は同義で、
  いずれが bootloader 障害の見かけでもある。古い bootloader が静かに失敗する原因は
  IDF 旧バージョンのマッピング実装(1MB 境界を跨ぐ IROM マッピング)と推定するが、
  古い bootloader の実体を保存していないため断定はしない
- 画像構造は esptool image-info で検証済み(セグメントの重なりや IRAM/DRAM オーバローは
  いずれのビルドにも無い)

## なぜ昨日(2026-11-18 午前)の判定が誤ったか — 手順上の教訓

1. **`PLATFORMIO_BUILD_FLAGS` を付けたビルドの後、フラグなしで upload すると再コンパイルが
   走り、別のバイナリがフラッシュされる**(upload 前に env が再評価される)。
   昨日の A/B のうち何度かは「実質フル版しか焼いていなかった」疑い
   → **教訓: env フラグを使うときはビルドと upload を同一フラグの 1 コマンドで行い、
   upload 出力の Compressed バイト数を識別しとして使う**
2. **PlatformIO のビルドキャッシュがリストア(cp 復元)を検出しない**ことがあり
   (mtime/ハッシュタイミング)、stale バイナリを焼いて解析を乱した
   → **教訓: ソースを差し替えたら必ず strings で elf に期待する文字列があるか確認**してから焼く
3. モニタ open 自体がリセットを掛け、**「アプリの起動ループ」と「モニタ再接続ループ」を混同**した
   → **教訓: 周期と回数は time フィルタ付きで測る**。モニタ open 直後の DOWNLOAD トラップ
   (rst:0x15 boot:0x4)は再焼き込みで回復できるが、**冷起動(USB 再挿)直後のモニタが最も安定**
4. ESPectre「○」(C3 対応表)= ライブラリの公式対応主張であって、本件は**そもそも
   bootloader までの問題**であり ESPectre を詐称とするのは誤り

## 残る検証(未確認)

- [ ] テザリング ON の状態で C3 が Wi-Fi 接続し、CSI(`occ` / `admitted_pps`)が流れ、
      動き検知→LINE push まで一通り通ること(本日のログは Wi-Fi 不在のため occ=0)
      ※なお probe 印字コード(`[probe] chip...`)が src/main.cpp に残っている。調査用であり
      次回セッションで除去してよい(ビルド時に PH_PAD_IROM を付けなければ pad.c も無害)
- [ ] `pio.ini` の C3 env 調査コメントの更新・handson への反映(C3 は「ブートローダー更新必須」の条件付き対応)
- [ ] ATOM Echo(ブートローダー既更新済み)は従来手順のままで問題ないことを再確認
- [ ] (任意)古い bootloader の原因追及: merged 焼き前のフラッシュ 0x0 領域を read-flash で
      保存してあると IDF 版同定が可能(未保存)
