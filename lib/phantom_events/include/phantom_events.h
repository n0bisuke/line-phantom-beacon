// 疑似 leave / 寝落ち疑い イベントの状態遷移 (Phase 4 の核となる純C++ロジック)。
// Arduino/ESP32 固有のAPIに依存しないので、実機なしでネイティブテストできる。
//
// 注意: このモジュールが扱うのは「疑似」イベントである。
// 「動きがない = 退室」「動きがない = 睡眠」ではない。
#pragma once

#include <stdint.h>

namespace phantom {

enum class Event {
  None,
  Motion,           // ESPectre が動きを検知 (静止状態から)
  PseudoLeave,      // 最後の動作検知から pseudo_leave_ms 経過
  SleepSuspected,   // 最後の動作検知から sleep_suspected_ms 経過 (1回だけ)
  MotionResumed,    // 静止状態 (leave / sleep 判定後) に再び動きを検知
};

// 位相 = 最後に動きを検知してからの「距離」を表す分類。
enum class Phase {
  Motion,   // 動き検知中 (しきい値内)
  Quiet,    // 静止しているが判定時間に未達
  Leave,    // pseudo_leave 判定済み
  Sleep,    // sleep_suspected 判定済み
};

class EventState {
 public:
  // leave_ms / sleep_ms は millis() と同じ単位のミリ秒。leave_ms < sleep_ms であること。
  EventState(uint32_t leave_ms, uint32_t sleep_ms);

  // ESPectre のモーション状態(デバウンス済みの bool)と現在時刻 millis() を与える。
  // 発生したイベントを1つ返す。同じ状態の連続通知を自動で抑止する。
  // - rising edge (false→true): Motion / MotionResumed を返す
  // - motion==true の間は最終検知時刻を keep-alive する(継続動作で擬似leaveが走らない)
  // - falling 後は threshold と現在時刻で PseudoLeave / SleepSuspected を返す
  Event update(bool motion, uint32_t now);

  Phase phase() const { return phase_; };
  uint32_t msSinceLastMotion(uint32_t now) const;

 private:
  Event onRisingEdge(uint32_t now);
  Event onQuietTick(uint32_t now);

  const uint32_t leave_ms_;
  const uint32_t sleep_ms_;

  Phase phase_ = Phase::Leave;  // 初回は「検知前静止状態」扱い(動いたら Motion に遷移)
  uint32_t last_motion_ms_ = 0;
  bool has_motion_ = false;     // 一度でも動きを検知したか
  bool leave_emitted_ = false;
  bool sleep_emitted_ = false;
};

const char *eventName(Event e);  // ログ用 ("motion", "pseudo_leave" など)

}  // namespace phantom
