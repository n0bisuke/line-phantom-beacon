// line-phantom-beacon Phase 1: ESPectre による Wi-Fi CSI 動作検知の単独動作
// - Wi-Fi (2.4GHz) に接続
// - ESPectre で CSI を取得し人の動きを検知
// - シリアルログに動作状態を表示
// - 動作検知時刻を記録(シリアルログ + 直近検知からの経過時間)
#include <Arduino.h>

#include "config.h"
#include "motion_sensor.h"

namespace {
MotionSensor s_motion;
unsigned long s_last_status_ms = 0;

void logNextStatus(unsigned long now) {
  if (now - s_last_status_ms < kStatusLogIntervalMs) {
    return;
  }
  s_last_status_ms = now;
  s_motion.logStatus(now);
}
}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(kSerialStartupDelayMs);  // 起動直後のみの短い待ち(シリアル安定化)
  Serial.println();
  Serial.println("[main] line-phantom-beacon Phase 1 (ESPectre Wi-Fi CSI)");
  s_motion.begin();
  s_last_status_ms = millis();
}

void loop() {
  s_motion.loop();
  logNextStatus(millis());
}
