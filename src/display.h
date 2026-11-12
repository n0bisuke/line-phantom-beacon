// AtomS3 の LCD(128x64) に動作状態を表示する (M5Unified 使用)。
// 表示するだけなのでロジックは持たない(状態は main から渡す)。
#pragma once

#include <Arduino.h>

// main が埋めて渡す表示用ステータス
struct DisplayStatus {
  bool wifi = false;
  bool ready = false;
  bool motion = false;
  float movement = 0.0f;
  bool has_motion = false;         // 一度でも動きを検知したか
  unsigned long idle_ms = 0;       // 最終検知からの経過 (ms)
  unsigned long count = 0;         // 動作検知 edge 回数
  unsigned long up_ms = 0;         // 起動からの経過 (ms)
};

class StatusDisplay {
 public:
  void begin();
  // 起動診断用: 段階番号とラベルを大きく表示する (0 BOOT / 1 WIFI / 2 ESPECTRE / 3 RUN)
  void showBootStage(int stage, const char *label);
  void showMessage(const char *label);
  void update(const DisplayStatus &s);

 private:
  bool initialized_ = false;
};
