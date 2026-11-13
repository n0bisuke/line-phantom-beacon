// line-phantom-beacon: ESPectre 動作検知 → 疑似イベント → LINE Messaging API 直接 push
// - Wi-Fi (2.4GHz) に接続し、ESPectre で CSI 動き検知
// - 遷移イベント(motion / pseudo_leave / continued / sleep_suspected / motion_resumed)を生成
// - LINE Messaging API へ ESP32 から直接 push(サーバーレス構成)
// - シリアルログ + AtomS3 LCD に動作状態を表示
#include <Arduino.h>

#include "config.h"
#include "display.h"
#include "line_push_client.h"
#include "motion_sensor.h"
#include "phantom_events.h"
#include "secrets.h"

namespace {
MotionSensor s_motion;
StatusDisplay s_display;
LinePushClient s_line;
// 疑似イベント状態機(純C++, ネイティブテスト済)。しきい値は secrets.h。
phantom::EventState s_events(PSEUDO_LEAVE_MS, PSEUDO_CONTINUED_MS, SLEEP_SUSPECTED_MS);
bool s_last_ready = false;
unsigned long s_last_status_ms = 0;
unsigned long s_last_display_ms = 0;

// 通知パターン(ユーザー仕様・3段階): 動き検知=push、10秒無動作=「動きがなくなりました」、
// 20秒=「継続して動きがありません」、30秒=寝落ち。寝落ち以降は push しない。
// MotionResumed は「動き検知」と同じ通知に正規化する。
const char *pushTypeFor(phantom::Event event) {
  switch (event) {
    case phantom::Event::Motion:
    case phantom::Event::MotionResumed:
      return "motion";
    case phantom::Event::PseudoLeave:
      return "pseudo_leave";
    case phantom::Event::PseudoLeaveContinued:
      return "pseudo_leave_continued";
    case phantom::Event::SleepSuspected:
      return "sleep_suspected";
    default:
      return nullptr;
  }
}

// 遷移イベントをシリアル + LINE push へ。Wi-Fi 未接続時は push を諦める。
void onPseudoEvent(phantom::Event event, unsigned long now) {
  const char *type = pushTypeFor(event);
  if (type == nullptr) {
    return;
  }
  Serial.printf("[event] %s (%lu ms)\n", type, now);
  if (s_motion.wifiConnected()) {
    s_line.push(type, now);
  } else {
    Serial.println("[line] skipped (Wi-Fi 未接続)");
  }
}

void feedEvents(unsigned long now) {
  // sensing が ready(信頼可能)の間だけ疑似イベントを評価する。
  // ready が落ちた期間の motion/静止はノイズなので無視し、復帰したら状態機を初期化する。
  const bool ready = s_motion.ready();
  if (ready != s_last_ready) {
    s_last_ready = ready;
    if (ready) {
      s_events.reset(now);
      Serial.println("[event] sensing ready (状態機初期化)");
    }
  }
  if (!ready) {
    return;
  }
  const phantom::Event event = s_events.update(s_motion.motion(), now);
  if (event != phantom::Event::None) {
    onPseudoEvent(event, now);
  }
}

void logNextStatus(unsigned long now) {
  if (now - s_last_status_ms < kStatusLogIntervalMs) {
    return;
  }
  s_last_status_ms = now;
  s_motion.logStatus(now);
}

DisplayStatus collectDisplayStatus(unsigned long now) {
  DisplayStatus s;
  s.wifi = s_motion.wifiConnected();
  s.ready = s_motion.ready();
  s.motion = s_motion.motion();
  s.movement = s_motion.movement();
  const unsigned long idle = s_motion.msSinceLastMotion();
  s.has_motion = (idle != ULONG_MAX);
  s.idle_ms = s.has_motion ? idle : 0;
  // 疑似イベントの位相を LCD の状態表示(暗青/紫)に反映
  const phantom::Phase ph = s_events.phase();
  s.state = (ph == phantom::Phase::Sleep) ? 2 : (ph == phantom::Phase::Leave) ? 1 : 0;
  s.count = s_motion.motionCount();
  s.up_ms = now;
  return s;
}

void updateDisplay(unsigned long now) {
  if (now - s_last_display_ms < kDisplayUpdateIntervalMs) {
    return;
  }
  s_last_display_ms = now;
  s_display.update(collectDisplayStatus(now));
}
}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(kSerialStartupDelayMs);  // 起動直後のみの短い待ち(シリアル安定化)
  Serial.println();
  Serial.println("[main] line-phantom-beacon (CSI 検知 + 疑似イベント + LINE直接push)");
  // 先に LCD を見えるようにして、どの段階で失敗しても画面に残す(診断用)
  s_display.begin();
  s_display.showBootStage(0, "BOOT");
  s_display.showBootStage(1, "WIFI");  // Wi-Fi 接続は begin() 内で最大20秒ブロック
  s_motion.begin();
  s_display.showBootStage(2, "RUN");
  s_line.begin();
  s_last_status_ms = millis();
  s_last_display_ms = millis();
}

void loop() {
  s_motion.loop();
  const unsigned long now = millis();
  feedEvents(now);
  logNextStatus(now);
  updateDisplay(now);
}
