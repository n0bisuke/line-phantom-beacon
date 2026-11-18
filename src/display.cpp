#include "display.h"

#ifdef PHANTOM_NO_DISPLAY
// ---------------------------------------------------------------
// LCD の無いボード (XIAO S3、M5Stack ATOM Echo 系など) 用のプレースホルダ。
// 描画 API はそのまま使い、起動ステージだけをシリアルに出す。
// 状態表示はシリアルログ([status] / [csi] / [event])で確認する。
// さらに PHANTOM_SPEAKER_PIN (<GPIO番号) が定義されたボードではビープ音で状態を報せる:
//   - 起動ステージごとに音程の上がる短いビープ
//   - キャリブレーション中は数秒ごとに低いビープ(進捗)
//   - キャリブレーション完了で上昇音
// ---------------------------------------------------------------
#ifdef PHANTOM_SPEAKER_PIN

// ビープ。ATOM Echo のスピーカーは Class-D アンプを DAC(GPIO25) で駆動する構成のため
// LEDC の PWM 方形波では鳴らない(2026-11-18 実機確認)。そこで DAC 出力 + esp_timer で
// 方形波を発生させる。update() が 250ms 間隔のため停止は最長 250ms 遅れる(十分鳴る長さ)。
#if PHANTOM_SPEAKER_PIN == 25
#define PH_DAC_CHANNEL DAC_CHANNEL_1
#else
#define PH_DAC_CHANNEL DAC_CHANNEL_2
#endif

#include "driver/dac_oneshot.h"
#include "esp_timer.h"

static dac_oneshot_handle_t s_dac = nullptr;
static esp_timer_handle_t s_tone_timer = nullptr;
static bool s_tone_level = false;
static unsigned long s_tone_until_ms = 0;

static void ph_tone_tick(void * /*unused*/) {
  s_tone_level = !s_tone_level;
  dac_oneshot_output_voltage(s_dac, s_tone_level ? 150 : 0);
}

void toneTimerStart(int freq) {
  if (s_tone_timer != nullptr) {
    esp_timer_stop(s_tone_timer);
    esp_timer_delete(s_tone_timer);
    s_tone_timer = nullptr;
  }
  s_tone_level = false;
  const esp_timer_create_args_t args = {
      .callback = &ph_tone_tick, .name = "ph_beep",
  };
  ESP_ERROR_CHECK(esp_timer_create(&args, &s_tone_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(s_tone_timer, 1000000ULL / (2 * freq)));
}

void toneStop() {
  if (s_tone_timer != nullptr) {
    esp_timer_stop(s_tone_timer);
    esp_timer_delete(s_tone_timer);
    s_tone_timer = nullptr;
  }
  dac_oneshot_output_voltage(s_dac, 0);
}

void beepInit() {
  const dac_oneshot_config_t cfg = {.chan_id = PH_DAC_CHANNEL};
  const esp_err_t err = dac_oneshot_new_channel(&cfg, &s_dac);
  if (err == ESP_OK) {
    dac_oneshot_output_voltage(s_dac, 0);
    Serial.printf("[display] speaker via DAC GPIO%d (ビープ通知あり)\n", (int)PHANTOM_SPEAKER_PIN);
  } else {
    Serial.printf("[display] DAC init failed: %d (ビープなし)\n", (int)err);
  }
}

// 短いビープを鳴らし、その場で確実に止める(起動ステージ用。setup 中の安全なブロック)
void beepOnce(int freq, int ms) {
  if (s_dac == nullptr) {
    return;
  }
  toneTimerStart(freq);
  delay(ms);
  toneStop();
}

// update() の 250ms リズム内で完結する非同期ビープ
void beepLater(int freq) {
  if (s_dac == nullptr) {
    return;
  }
  toneTimerStart(freq);
  s_tone_until_ms = millis() + 60;
}

void beepTick() {
  if (s_dac == nullptr) {
    return;
  }
  if (s_tone_until_ms != 0 && (long)(millis() - s_tone_until_ms) >= 0) {
    toneStop();
    s_tone_until_ms = 0;
  }
}

#else

void beepInit() {}
void beepOnce(int, int) {}
void beepLater(int) {}
void beepTick() {}

#endif  // PHANTOM_SPEAKER_PIN

// 内蔵 LED (SK6812/WS2812 系)。Echo は 1 個 (GPIO27)。neopixelWrite は
// RMT ベースなのでブロックしない(1度に~µsオーダ)。
#ifdef PHANTOM_LED_PIN

// 暗く保つ(Echo の LED は非常に明るい)。全色で最大 127/255 まで。
static void ledSet(unsigned r, unsigned g, unsigned b) {
  neopixelWrite(PHANTOM_LED_PIN, (r & 0x7F), (g & 0x7F), (b & 0x7F));
}

#else

void ledSet(unsigned, unsigned, unsigned) {}

#endif  // PHANTOM_LED_PIN

// 単色オンボード LED(XIAO ESP32-C3 の GPIO3 など)。多色表示はできず、
// 点滅パターンで状態を表す。アクティブ LOW(LOW で点灯)のボードが多いことに注意。
#ifdef PHANTOM_GPIO_LED_PIN

enum class GpioLedMode {
  On,          // 常時点灯(監視中)
  Off,         // 消灯(静止=LEAVE/SLEEP。うるさくしない)
  SlowBlink,   // 1秒周期(キャリブレーション中)
  FastBlink,   // 150ms周期(動き検知中/Wi-Fiなし)
};

static bool s_gpio_led_ready = false;
static GpioLedMode s_gpio_led_mode = GpioLedMode::On;

static void gpioLedInit() {
  if (!s_gpio_led_ready) {
    pinMode(PHANTOM_GPIO_LED_PIN, OUTPUT);
    s_gpio_led_ready = true;
  }
}

static void gpioLedSet(GpioLedMode mode) {
  gpioLedInit();
  s_gpio_led_mode = mode;
}

static void gpioLedTick() {
  if (!s_gpio_led_ready) {
    return;
  }
  bool lit;
  const unsigned long t = millis();
  switch (s_gpio_led_mode) {
    case GpioLedMode::On:
      lit = true;
      break;
    case GpioLedMode::Off:
      lit = false;
      break;
    case GpioLedMode::SlowBlink:
      lit = (t / 1000UL) % 2 == 0;
      break;
    case GpioLedMode::FastBlink:
    default:
      lit = (t / 150UL) % 2 == 0;
      break;
  }
  // active LOW: LOW で点灯するボード(XIAO ESP32-C3)向けの反転
  digitalWrite(PHANTOM_GPIO_LED_PIN, lit ? LOW : HIGH);
}

#else

enum class GpioLedMode { On, Off, SlowBlink, FastBlink };
static void gpioLedInit() {}
static void gpioLedSet(GpioLedMode) {}
static void gpioLedTick() {}

#endif  // PHANTOM_GPIO_LED_PIN

void StatusDisplay::begin() {
  beepInit();
  gpioLedSet(GpioLedMode::On);
  Serial.println("[display] no LCD on this board (PHANTOM_NO_DISPLAY, 状態はシリアルログで確認)");
}

void StatusDisplay::showBootStage(int stage, const char *label) {
  Serial.printf("[display] stage %d: %s\n", stage, label ? label : "");
  beepOnce(800 + stage * 200, 70);        // BOOT→WIFI→RUN と音程が上がる
  ledSet(0x30, 0x30, 0x30);               // 起動中は白(dim)
  gpioLedSet(GpioLedMode::On);            // 単色 LED があるボードでは起動中=点灯
}

void StatusDisplay::showMessage(const char *label) {
  Serial.printf("[display] message: %s\n", label ? label : "");
}

void StatusDisplay::update(const DisplayStatus &s) {
  beepTick();

  static bool s_was_ready = false;
  static unsigned long s_last_calib_beep_ms = 0;

  if (s.ready && !s_was_ready) {
    beepLater(1600);   // キャリブレーション完了: ピロリン(高め)
    s_last_calib_beep_ms = millis();
  }
  s_was_ready = s.ready;

  if (!s.ready && s.wifi) {
    // キャリブレーション進捗: 3秒ごとに低いビープ
    if (millis() - s_last_calib_beep_ms > 3000UL) {
      beepLater(600);
      s_last_calib_beep_ms = millis();
    }
  }

  // 単色 LED と多色 LED の状態(LCD ありボードの配色に合わせる)
  if (!s.wifi) {
    gpioLedSet(GpioLedMode::FastBlink);
    ledSet(0x60, 0x18, 0x00);     // Wi-Fi 切断: オレンジ
  } else if (!s.ready) {
    gpioLedSet(GpioLedMode::SlowBlink);
    ledSet(0x00, 0x10, 0x50);     // キャリブレーション中: 青(動かさない)
  } else if (s.motion) {
    gpioLedSet(GpioLedMode::FastBlink);
    ledSet(0x60, 0x00, 0x00);     // 動き検知中: 赤
  } else {
    switch (s.state) {
      case 2:
        gpioLedSet(GpioLedMode::Off);  // LEAVE/SLEEP は単色 LED を消す(静寂)
        ledSet(0x30, 0x00, 0x40);      // 寝落ち(紫)
        break;
      case 1:
        gpioLedSet(GpioLedMode::Off);
        ledSet(0x00, 0x30, 0x30);      // 疑似 leave(シアン)
        break;
      default:
        gpioLedSet(GpioLedMode::On);   // 正常監視中
        ledSet(0x00, 0x28, 0x00);      // 正常監視中: 緑(dim)
        break;
    }
  }
  gpioLedTick();
}

#else  // ---- LCD あり(AtomS3 等。M5Unified 使用) ----
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
#endif  // PHANTOM_NO_DISPLAY
