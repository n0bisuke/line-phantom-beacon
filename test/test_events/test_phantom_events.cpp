// lib/phantom_events (Phase 4 状態遷移) のネイティブテスト。
// 実行: pio test -e native  (ボード不要、コンテナ内で完結)
#include <unity.h>

#include "phantom_events.h"

using phantom::Event;
using phantom::EventState;
using phantom::Phase;

namespace {
const uint32_t kLeaveMs = 30000;     // 第1段階: 疑似 leave 判定時間
const uint32_t kContinueMs = 60000;  // 第2段階: 「まだ動きがない」
const uint32_t kSleepMs = 90000;     // 第3段階: 寝落ち疑い(最終段階)

EventState makeDefault() {
  return EventState(kLeaveMs, kContinueMs, kSleepMs);
}
}  // namespace

void setUp() {}
void tearDown() {}

// 起動直後は静止が続いても空室通知をしない。初回の動き → Motion
void test_startup_quiet_then_first_motion() {
  EventState s = makeDefault();
  TEST_ASSERT_EQUAL(Event::None, s.update(false, 0));
  TEST_ASSERT_EQUAL(Event::None, s.update(false, 60000));
  TEST_ASSERT_EQUAL(Event::Motion, s.update(true, 61000));
  TEST_ASSERT_EQUAL(Phase::Motion, s.phase());
}

// 10段階目: leave しきい値で PseudoLeave。継続は Continued、最後は Sleep で順に1回ずつ
void test_three_stage_quiet_sequence() {
  EventState s = makeDefault();
  TEST_ASSERT_EQUAL(Event::Motion, s.update(true, 0));
  TEST_ASSERT_EQUAL(Event::None, s.update(false, kLeaveMs - 1));
  TEST_ASSERT_EQUAL(Event::PseudoLeave, s.update(false, kLeaveMs));
  TEST_ASSERT_EQUAL(Phase::Leave, s.phase());
  TEST_ASSERT_EQUAL(Event::None, s.update(false, kLeaveMs + 10));  // 同じ期間では再通知しない
  TEST_ASSERT_EQUAL(Event::PseudoLeaveContinued, s.update(false, kContinueMs));
  TEST_ASSERT_EQUAL(Event::SleepSuspected, s.update(false, kSleepMs));
  TEST_ASSERT_EQUAL(Phase::Sleep, s.phase());
  TEST_ASSERT_EQUAL(Event::None, s.update(false, kSleepMs + 60000));  // 延長戦なし
}

// 寝落ちまで一気に時間が経った場合: SleepSuspected だけが出る(leave 系の遅延発火なし)
void test_long_quiet_jumps_to_sleep() {
  EventState s = makeDefault();
  TEST_ASSERT_EQUAL(Event::Motion, s.update(true, 0));
  TEST_ASSERT_EQUAL(Event::None, s.update(false, kLeaveMs - 1));  // leave 未達
  TEST_ASSERT_EQUAL(Event::SleepSuspected, s.update(false, kSleepMs));
  TEST_ASSERT_EQUAL(Event::None, s.update(false, kSleepMs + 5));
}

// leave 判定後に再検知 → MotionResumed。フラグはリセットされるので再静止でまた PseudoLeave
void test_motion_resumed_after_leave_and_cycle_repeats() {
  EventState s = makeDefault();
  TEST_ASSERT_EQUAL(Event::Motion, s.update(true, 0));
  TEST_ASSERT_EQUAL(Event::PseudoLeave, s.update(false, kLeaveMs));
  TEST_ASSERT_EQUAL(Event::MotionResumed, s.update(true, kLeaveMs + 5000));
  TEST_ASSERT_EQUAL(Phase::Motion, s.phase());
  TEST_ASSERT_EQUAL(Event::PseudoLeave, s.update(false, kLeaveMs + 5000 + kLeaveMs));
}

// sleep 判定後の再検知も MotionResumed
void test_motion_resumed_after_sleep() {
  EventState s = makeDefault();
  TEST_ASSERT_EQUAL(Event::Motion, s.update(true, 0));
  TEST_ASSERT_EQUAL(Event::SleepSuspected, s.update(false, kSleepMs));
  TEST_ASSERT_EQUAL(Event::MotionResumed, s.update(true, kSleepMs + 10000));
  TEST_ASSERT_EQUAL(Phase::Motion, s.phase());
  TEST_ASSERT_EQUAL(Event::None, s.update(false, kSleepMs + 10010));  // falling 後は Quiet 位相
}

// motion==true が継続している間は擬似 leave が走らない (keep-alive)
void test_continuous_motion_never_goes_quiet() {
  EventState s = makeDefault();
  TEST_ASSERT_EQUAL(Event::Motion, s.update(true, 0));
  for (uint32_t t = 1000; t <= 600000; t += 1000) {
    TEST_ASSERT_EQUAL_MESSAGE(Event::None, s.update(true, t), "continuous motion t");
  }
  TEST_ASSERT_EQUAL(Phase::Motion, s.phase());
}

// falling 後しきい値未満で再検知 → 位相は Quiet → Motion(再検知イベント)。leave/resumed 扱いにしない
void test_short_quiet_gap_is_plain_motion() {
  EventState s = makeDefault();
  TEST_ASSERT_EQUAL(Event::Motion, s.update(true, 0));
  TEST_ASSERT_EQUAL(Event::None, s.update(false, 2000));
  TEST_ASSERT_EQUAL(Phase::Quiet, s.phase());
  TEST_ASSERT_EQUAL(Event::Motion, s.update(true, 4000));
  TEST_ASSERT_EQUAL(Event::None, s.update(false, 4001));  // Quiet 帰りで重複しない
}

// しきい値未満では何も出さない
void test_below_threshold_no_events() {
  EventState s = makeDefault();
  TEST_ASSERT_EQUAL(Event::Motion, s.update(true, 0));
  TEST_ASSERT_EQUAL(Event::None, s.update(true, kLeaveMs - 1));  // 動いていれば何も出さない
}

// ready 復帰用 reset: クロック再スタート + 発行済みフラグのクリア
void test_reset_starts_quiet_clock_and_clears_flags() {
  EventState s = makeDefault();
  TEST_ASSERT_EQUAL(Event::Motion, s.update(true, 1000));
  TEST_ASSERT_EQUAL(Event::PseudoLeave, s.update(false, 1000 + kLeaveMs));  // 一度発火させておく
  // ready 復帰を想定した reset: 検知クロックは now から再スタート
  s.reset(200000);
  TEST_ASSERT_EQUAL(0u, s.msSinceLastMotion(200000));
  TEST_ASSERT_EQUAL(Event::None, s.update(false, 200000 + kLeaveMs - 1));    // 未達では出ない
  TEST_ASSERT_EQUAL(Event::PseudoLeave, s.update(false, 200000 + kLeaveMs)); // 新しい静止期間で1回
  // reset 直後に motion が来た場合、MotionResumed ではなく Motion(=動き検知)扱いになる
  s.reset(300000);
  TEST_ASSERT_EQUAL(Event::Motion, s.update(true, 300100));
}

// millis() の wrap に対する耐性(32bit lap の直後でも正の経過差分が出る)
void test_wrap_tolerant_elapsed() {
  EventState s = makeDefault();
  const uint32_t near_wrap = 4294967290u;  // UINT32_MAX - 5
  TEST_ASSERT_EQUAL(Event::Motion, s.update(true, near_wrap));
  TEST_ASSERT_EQUAL_MESSAGE(5u, s.msSinceLastMotion(near_wrap + 5u), "small elapsed after wrap");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_startup_quiet_then_first_motion);
  RUN_TEST(test_three_stage_quiet_sequence);
  RUN_TEST(test_long_quiet_jumps_to_sleep);
  RUN_TEST(test_motion_resumed_after_leave_and_cycle_repeats);
  RUN_TEST(test_motion_resumed_after_sleep);
  RUN_TEST(test_continuous_motion_never_goes_quiet);
  RUN_TEST(test_short_quiet_gap_is_plain_motion);
  RUN_TEST(test_below_threshold_no_events);
  RUN_TEST(test_reset_starts_quiet_clock_and_clears_flags);
  RUN_TEST(test_wrap_tolerant_elapsed);
  return UNITY_END();
}
