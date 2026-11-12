#include "phantom_events.h"

namespace phantom {

EventState::EventState(uint32_t leave_ms, uint32_t sleep_ms)
    : leave_ms_(leave_ms), sleep_ms_(sleep_ms) {
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
    // 寝落ち疑いは退室疑い(pseudo_leave)を包含する。sleep_suspected 発火時点で
    // leave も出済み扱いにし、後続 tick で pseudo_leave だけが遅れて発火しないようにする。
    sleep_emitted_ = true;
    leave_emitted_ = true;
    phase_ = Phase::Sleep;
    return Event::SleepSuspected;
  }
  if (quiet >= leave_ms_ && !leave_emitted_) {
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
    case Event::SleepSuspected:
      return "sleep_suspected";
    case Event::MotionResumed:
      return "motion_resumed";
    default:
      return nullptr;
  }
}

}  // namespace phantom
