# 開発ログ 2026-11-13

## 3段階疑似イベント仕様の実装確定 (10s / 20s / 30s)

ユーザー仕様(最終形):
1. 動き検知 → 🏃 push(カウントは 0 に戻り再カウント)
2. 10秒無動作 → 🚪「動きがなくなりました(10秒...)」push + LCD 背景を暗青に変更
3. 20秒無動作 → 🚪「継続して動きがありません(20秒...)」push
4. 30秒無動作 → 😴「寝落ちの可能性(30秒...)」push + LCD 背景を紫に変更(カウントは延長戦で続く)
5. 30秒以降は push しない(通知も LCD も状態を保持)
- push 文言に秒数を含める(secrets.h のしきい値から自動生成)

### 変更内容
- `lib/phantom_events/`: `Event::PseudoLeaveContinued` 追加、3引数コンストラクタ
  (`leave_ms < continued_ms < sleep_ms`)、`reset()` 発行済みフラグすべて解消。
  静観Tickは descending(寝落ち→継続→疑似leave)に判定。**寝落ち発火後は下位段階を出さない**
  (大きく時間が飛んだ場合、leave/continued が後から遅延発火しない)
- `include/secrets.h` / `secrets.h.example`: `PSEUDO_LEAVE_MS=10000 / PSEUDO_CONTINUED_MS=20000 /
  SLEEP_SUSPECTED_MS=30000`(secrets.h.example から RELAY_TOKEN/RELAY_DEVICE_ID を削除=relay 廃止)
- `src/line_push_client.cpp`: `makeBody(makeText(type))` の型不整合を修正(makeBody を String 受けに)。
  4パターンの push 文言に秒数を自動付与
- `src/main.cpp`: 表示用に `s_events.phase()` を `DisplayStatus.state` に反映(LetterLeave=1, Sleep=2)。
  先頭コメント・push仕様コメントをサーバーレス/3段階仕様に更新
- `src/display.cpp` / `display.h`: 寝落ち=紫 `color565(0x3A,0x10,0x8C)`、疑似leave=暗青
  `color565(0x10,0x20,0x70)` の大きいカウント表示。上段に `LEAVE` / `SLEEP` を追加表示
- `test/test_events/`: 3引数 ctor に対応。`test_three_stage_quiet_sequence`(10→20→30→以降なし)、
  `test_long_quiet_jumps_to_sleep`(遅延発火なし)、reset 前に実際に発火させてフラグ解消を検証。
  **17/17 テスト通過**
- ビルド: `pio run` SUCCESS(Flash 54.7% / 1,827,007 bytes)
- README: 疑似イベント仕様表(4パターン)、push テキスト表、LCD 表示仕様に
  暗青/紫の2行追加、NO-WIFI 行追加

### ノイズ対応(ready gating)
🏃→🚪→👋 がローテーションする症状(CSI品質低下で ready が往復する日) → sensing が ready の
間だけイベント評価し、ready 復帰時に `s_events.reset(now)` で状態機を初期化。

### 未確認(実機)
書き込みは `pio remote` 用 agent 未起動のため未実施。書き込み後の確認項目:
1. 手を振る → 🏃 push
2. 10秒静止 → 🚪 push + LCD 暗青 + 上段 LEAVE
3. 20秒 → 継続 push
4. 30秒 → 😴 push + LCD 紫 + 上段 SLEEP
5. 30秒以降は push なし・カウントは継続
6. 再び動かす → 🏃 push・LCD 黒に復帰
