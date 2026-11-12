// lib/phantom_events (Phase 4 状態遷移) のネイティブテスト。
// 実行: pio test -e native  (ボード不要、コンテナ内で完結)
#include <unity.h>

#include "phantom_events.h"

using phantom::Event;
using phantom::EventState;
using phantom::Phase;

namespace {
const uint32_t kLeaveMs = 30000;    // 初期設定値: 疑似 leave 判定時間
const uint32_t kSleepMs = 180000;   // 初期設定値: 寝落ち疑い判定時間

EventState makeDefault() {
  return EventState(kLeaveMs, kSleepMs);
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

// 30秒(しきい値含む)静止 → PseudoLeave。以後同じ状態では再通知しない
void test_pseudo_leave_fires_once_at_threshold() {
  EventState s = makeDefault();
  TEST_ASSERT_EQUAL(Event::Motion, s.update(true, 0));
  TEST_ASSERT_EQUAL(Event::None, s.update(false, 10000));
  TEST_ASSERT_EQUAL(Event::None, s.update(false, kLeaveMs - 1));
  TEST_ASSERT_EQUAL(Event::PseudoLeave, s.update(false, kLeaveMs));
  TEST_ASSERT_EQUAL(Phase::Leave, s.phase());
  TEST_ASSERT_EQUAL(Event::None, s.update(false, kLeaveMs + 10));
  TEST_ASSERT_EQUAL(Event::None, s.update(false, 120000));
}

// 3分静止 → SleepSuspected。以後同じ状態では再通知しない
void test_sleep_suspected_fires_once_at_threshold() {
  EventState s = makeDefault();
  TEST_ASSERT_EQUAL(Event::Motion, s.update(true, 0));
  TEST_ASSERT_EQUAL(Event::SleepSuspected, s.update(false, kSleepMs));
  TEST_ASSERT_EQUAL(Phase::Sleep, s.phase());
  TEST_ASSERT_EQUAL(Event::None, s.update(false, kSleepMs + 60000));
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

// しきい値未満ではイベントを余計に出さない。30秒未達は無事名、30秒〜3分は疑似 leave、
// 3分未達は寝落ち疑いをまだ出さない
void test_below_threshold_no_events() {
  EventState s = makeDefault();
  TEST_ASSERT_EQUAL(Event::Motion, s.update(true, 0));
  TEST_ASSERT_EQUAL(Event::None, s.update(false, kLeaveMs - 1));   // 30秒未達
  TEST_ASSERT_EQUAL(Event::PseudoLeave, s.update(false, kSleepMs - 1));  // 30秒〜3分
  TEST_ASSERT_EQUAL(Event::None, s.update(false, kSleepMs - 1));   // 3分未達 (sleep はまだ)
};

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
  RUN_TEST(test_pseudo_leave_fires_once_at_threshold);
  RUN_TEST(test_sleep_suspected_fires_once_at_threshold);
  RUN_TEST(test_motion_resumed_after_leave_and_cycle_repeats);
  RUN_TEST(test_motion_resumed_after_sleep);
  RUN_TEST(test_continuous_motion_never_goes_quiet);
  RUN_TEST(test_short_quiet_gap_is_plain_motion);
  RUN_TEST(test_below_threshold_no_events);
  RUN_TEST(test_wrap_tolerant_elapsed);
  return UNITY_END();
}
