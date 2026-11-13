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

  // 上段: Wi-Fi / ready / movement スコア(Font2 = 16px高で Font0 の2倍の大きさ)
  M5.Display.fillRect(0, 0, 128, 16, TFT_BLACK);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  const char *extra = s.state == 2 ? "SLEEP" : s.state == 1 ? "LEAVE" : nullptr;
  if (extra != nullptr) {
    snprintf(line, sizeof(line), "W:%-2s R%d %s", s.wifi ? "on" : "--", s.ready ? 1 : 0, extra);
  } else {
    snprintf(line, sizeof(line), "W:%-2s R%d mv:%d", s.wifi ? "on" : "--", s.ready ? 1 : 0,
             (int)s.movement);
  }
  M5.Display.drawString(line, 2, 0);
  M5.Display.setFont(&fonts::Font0);  // 下段(大きな状態表示)は Font0 x size で描くため戻す

  // 下段(大 24x48): 今の状態をひと目で
  if (s.motion) {
    // 動きを検知している
    M5.Display.fillRect(0, 16, 128, 48, TFT_DARKGREEN);
    M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    M5.Display.setTextSize(3);
    M5.Display.drawCenterString("MOVE", M5.Display.width() / 2, 16);
  } else if (!s.wifi) {
    // Wi-Fi 未接続: ESPectre は CSI を取れないので CALIB と区別して表示する
    M5.Display.fillRect(0, 16, 128, 48, TFT_MAROON);
    M5.Display.setTextColor(TFT_ORANGE, TFT_MAROON);
    M5.Display.setTextSize(2);  // "NO-WIFI" は7文字なので size2(112px)で収める
    M5.Display.drawCenterString("NO-WIFI", M5.Display.width() / 2, 24);
  } else if (!s.ready) {
    // キャリブレーション中(起動後しばらく。動いちゃいけない)
    M5.Display.fillRect(0, 16, 128, 48, TFT_BLACK);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.setTextSize(3);
    M5.Display.drawCenterString("CALIB", M5.Display.width() / 2, 16);
  } else if (s.state == 2) {
    // 寝落ち(30s〜): カウントは続けるが背景を紫に変える
    M5.Display.fillRect(0, 16, 128, 48, M5.Display.color565(0x3A, 0x10, 0x8C));
    M5.Display.setTextColor(TFT_WHITE, M5.Display.color565(0x3A, 0x10, 0x8C));
    snprintf(line, sizeof(line), "%lus", s.idle_ms / 1000UL);
    M5.Display.setTextSize(3);
    M5.Display.drawCenterString(line, M5.Display.width() / 2, 16);
  } else if (s.state == 1) {
    // 疑似 leave(10s〜): 背景を暗青に変えて静かさを示す
    M5.Display.fillRect(0, 16, 128, 48, M5.Display.color565(0x10, 0x20, 0x70));
    M5.Display.setTextColor(TFT_WHITE, M5.Display.color565(0x10, 0x20, 0x70));
    snprintf(line, sizeof(line), "%lus", s.idle_ms / 1000UL);
    M5.Display.setTextSize(3);
    M5.Display.drawCenterString(line, M5.Display.width() / 2, 16);
  } else if (s.has_motion) {
    // 正常監視中: 最後の動きからの秒数を大きく
    M5.Display.fillRect(0, 16, 128, 48, TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(line, sizeof(line), "%lus", s.idle_ms / 1000UL);
    M5.Display.setTextSize(3);
    M5.Display.drawCenterString(line, M5.Display.width() / 2, 16);
  } else {
    // キャリブレーション済みだがまだ一度も動きを検知していない
    M5.Display.fillRect(0, 16, 128, 48, TFT_BLACK);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.setTextSize(3);
    M5.Display.drawCenterString("WAIT", M5.Display.width() / 2, 16);
  }
  M5.Display.setTextSize(1);
}
