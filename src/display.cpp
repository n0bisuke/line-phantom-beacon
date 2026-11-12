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
  // 上段に小さくラベル、中央に大きく段階番号(size3 = 24x48px)。
  // ラベルを size2 で描くと 128px を溢れて文字が崩れるため小さいまま。
  char buf[24];
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setTextSize(1);
  snprintf(buf, sizeof(buf), "%s", label ? label : "");
  M5.Display.drawCenterString(buf, M5.Display.width() / 2, 4);
  snprintf(buf, sizeof(buf), "%d", stage);
  M5.Display.setTextSize(3);
  M5.Display.drawCenterString(buf, M5.Display.width() / 2, 16);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void StatusDisplay::update(const DisplayStatus &s) {
  if (!initialized_) {
    return;
  }
  char line[17];

  // 上段(小): Wi-Fi / ready / movement スコア
  M5.Display.fillRect(0, 0, 128, 16, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  snprintf(line, sizeof(line), "W:%-2s R%d mv:%5d", s.wifi ? "on" : "--", s.ready ? 1 : 0,
           (int)s.movement);
  M5.Display.drawString(line, 2, 4);

  // 下段(大 24x48): 今の状態をひと目で
  if (s.motion) {
    // 動きを検知している
    M5.Display.fillRect(0, 16, 128, 48, TFT_DARKGREEN);
    M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    M5.Display.setTextSize(3);
    M5.Display.drawCenterString("MOVE", M5.Display.width() / 2, 16);
  } else if (s.has_motion) {
    // 正常監視中: 最後の動きからの秒数を大きく
    M5.Display.fillRect(0, 16, 128, 48, TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(line, sizeof(line), "%lus", s.idle_ms / 1000UL);
    M5.Display.setTextSize(3);
    M5.Display.drawCenterString(line, M5.Display.width() / 2, 16);
  } else if (!s.ready) {
    // キャリブレーション中(起動後しばらく。動いちゃいけない)
    M5.Display.fillRect(0, 16, 128, 48, TFT_BLACK);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.setTextSize(3);
    M5.Display.drawCenterString("CALIB", M5.Display.width() / 2, 16);
  } else {
    // キャリブレーション済みだがまだ一度も動きを検知していない
    M5.Display.fillRect(0, 16, 128, 48, TFT_BLACK);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.setTextSize(3);
    M5.Display.drawCenterString("WAIT", M5.Display.width() / 2, 16);
  }
  M5.Display.setTextSize(1);
}
