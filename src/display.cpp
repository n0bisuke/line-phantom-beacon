#include "display.h"

#include <M5Unified.h>

// 128x64 / font0 8x16px → 4行 x 16文字
void StatusDisplay::begin() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(0);
  M5.Display.setBrightness(220);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.clear();
  initialized_ = true;
}

void StatusDisplay::showMessage(const char *label) {
  if (!initialized_) {
    return;
  }
  M5.Display.clear();
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.drawCenterString(label, M5.Display.width() / 2, 20);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void StatusDisplay::showBootStage(int stage, const char *label) {
  if (!initialized_) {
    return;
  }
  M5.Display.clear();
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  char line[17];
  snprintf(line, sizeof(line), "S%d %s", stage, label);
  M5.Display.setTextSize(2);
  M5.Display.drawString(line, 2, 24);
  snprintf(line, sizeof(line), "stage %d/3", stage);
  M5.Display.setTextSize(1);
  M5.Display.drawString(line, 2, 56);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void StatusDisplay::update(const DisplayStatus &s) {
  if (!initialized_) {
    return;
  }
  char line[17];

  // 行1: WiFi / ready / motion
  M5.Display.fillRect(0, 0, 128, 16, s.motion ? TFT_DARKGREEN : TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, s.motion ? TFT_DARKGREEN : TFT_BLACK);
  snprintf(line, sizeof(line), "W%-3sR%d  %s", s.wifi ? "on" : "off", s.ready ? 1 : 0,
           s.motion ? "MOVE" : "----");
  M5.Display.drawString(line, 2, 0);

  // 行2: movement スコア
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(line, sizeof(line), "mv:%06.1f", s.movement);
  M5.Display.drawString(line, 2, 16);

  // 行3: 最終検知からの経過(起動後初検知前は ---)
  if (s.has_motion) {
    const unsigned long sec = s.idle_ms / 1000UL;
    snprintf(line, sizeof(line), "idle:%5lus sec", sec);
  } else {
    snprintf(line, sizeof(line), "idle:    ---");
  }
  M5.Display.drawString(line, 2, 32);

  // 行4: 検知回数とuptime
  snprintf(line, sizeof(line), "cnt:%03lu up:%lus", s.count, s.up_ms / 1000UL);
  M5.Display.drawString(line, 2, 48);
}
