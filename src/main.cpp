// line-phantom-beacon Phase 1: ESPectre による Wi-Fi CSI 動作検知の単独動作
// - Wi-Fi (2.4GHz) に接続
// - ESPectre で CSI を取得し人の動きを検知
// - シリアルログ + AtomS3 LCD に動作状態を表示
// - 動作検知時刻を記録(シリアルログ + 直近検知からの経過時間)
#include <Arduino.h>

#include "config.h"
#include "display.h"
#include "motion_sensor.h"

namespace {
MotionSensor s_motion;
StatusDisplay s_display;
unsigned long s_last_status_ms = 0;
unsigned long s_last_display_ms = 0;

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
  Serial.println("[main] line-phantom-beacon Phase 1 (ESPectre Wi-Fi CSI)");
  // 先に LCD を見えるようにして、どの段階で失敗しても画面に残す(診断用)
  s_display.begin();
  s_display.showBootStage(0, "BOOT");
  s_display.showBootStage(1, "WIFI");  // Wi-Fi 接続は begin() 内で最大20秒ブロック
  s_motion.begin();
  s_display.showBootStage(2, "RUN");
  s_last_status_ms = millis();
  s_last_display_ms = millis();
}

void loop() {
  s_motion.loop();
  const unsigned long now = millis();
  logNextStatus(now);
  updateDisplay(now);
}
