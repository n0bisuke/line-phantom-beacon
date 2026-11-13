#include "phantom_events.h"

namespace phantom {

void EventState::reset(uint32_t now) {
  // sensing 再開(ready 復帰)時の初期化: 「検知実績あり・直前に動いた」という仮定で
  // 静観開始とする。ノイズで ready が落ち復帰しても、その直後の擬似 leave は
  // 10秒の静観後にしか出ず、MotionResumed も出さないため push が回転しない。
  // leave / sleep emitted フラグも解消するため、新しい静観期間で 1 回だけ通知される。
  phase_ = Phase::Quiet;
  last_motion_ms_ = now;
  has_motion_ = true;
  leave_emitted_ = false;
  continued_emitted_ = false;
  sleep_emitted_ = false;
}

EventState::EventState(uint32_t leave_ms, uint32_t continued_ms, uint32_t sleep_ms)
    : leave_ms_(leave_ms), continued_ms_(continued_ms), sleep_ms_(sleep_ms) {
  // 対称性のため初期状態は leave_emitted 扱いにしない: 起動直後に誰もいなくても
  // 検知実績がないので「退室」の通知は意味がない。rising edge でのみ解除する。
}

Event EventState::update(bool motion, uint32_t now) {
  if (motion) {
    return onRisingEdge(now);
  }
  return onQuietTick(now);
}

Event EventState::onRisingEdge(uint32_t now) {
  // 継続検知中 (phase が Motion) は keep-alive のみ
  last_motion_ms_ = now;
  has_motion_ = true;

  if (phase_ == Phase::Motion) {
    return Event::None;
  }

  // 静止状態 (Leave / Sleep / 初回) からの再検知
  phase_ = Phase::Motion;
  const Event e = (leave_emitted_ || sleep_emitted_) ? Event::MotionResumed : Event::Motion;
  leave_emitted_ = false;
  sleep_emitted_ = false;
  return e;
}

Event EventState::onQuietTick(uint32_t now) {
  const uint32_t quiet = msSinceLastMotion(now);
  if (!has_motion_) {
    // 検知実績ゼロの長時間静止は通知対象にしない(起動直後の空室通知を防ぐ)
    phase_ = Phase::Leave;
    return Event::None;
  }
  if (phase_ == Phase::Motion && quiet > 0) {
    phase_ = Phase::Quiet;  // falling edge
  }

  if (quiet >= sleep_ms_ && !sleep_emitted_) {
    // 寝落ち疑いは最終段階。発火済み後は何も返さない(延長戦なし)
    sleep_emitted_ = true;
    phase_ = Phase::Sleep;
    return Event::SleepSuspected;
  }
  // 寝落ち(Sleep)が既に発火していたら下位段階は出さない(順番を飛び越えた発火防止)
  if (!sleep_emitted_ && quiet >= continued_ms_ && !continued_emitted_) {
    continued_emitted_ = true;
    phase_ = Phase::Leave;  // 「まだ静か」段階。表示は疑似leaveと同じ
    return Event::PseudoLeaveContinued;
  }
  if (!sleep_emitted_ && quiet >= leave_ms_ && !leave_emitted_) {
    leave_emitted_ = true;
    phase_ = Phase::Leave;
    return Event::PseudoLeave;
  }
  return Event::None;
}

uint32_t EventState::msSinceLastMotion(uint32_t now) const {
  if (!has_motion_) {
    return UINT32_MAX;
  }
  return now - last_motion_ms_;  // 符号なし差分で millis() のwrapにも耐える
}

const char *eventName(Event e) {
  switch (e) {
    case Event::Motion:
      return "motion";
    case Event::PseudoLeave:
      return "pseudo_leave";
    case Event::PseudoLeaveContinued:
      return "pseudo_leave_continued";
    case Event::SleepSuspected:
      return "sleep_suspected";
    case Event::MotionResumed:
      return "motion_resumed";
    default:
      return nullptr;
  }
}

}  // namespace phantom
