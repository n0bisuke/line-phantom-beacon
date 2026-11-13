// Wi-Fi 接続 + ESPectre (Wi-Fi CSI 動作検知) のラッパ。
// ESPectre 公式サンプル (espectre-arduino examples/MotionDetection, v3.2.0) を母体に
// millis() ベースの再接続・ステータス記録を加えたもの。
#pragma once

#include <Arduino.h>

// 動作検知の状態と統計を保持し、シリアルログに出力する。
class MotionSensor {
 public:
  void begin();  // Wi-Fi 接続(ブロック付き) + ESPectre 初期化
  void loop();   // 毎ループ呼び出し: ESPectre ポンプ + Wi-Fi 再接続

  bool wifiConnected() const { return wifi_connected_; };
  unsigned long motionCount() const { return motion_count_; };
  bool ready() const { return ready_; };          // 検知結果が信頼できるか(キャリブレーション完了)
  bool motion() const { return in_motion_; };     // 現在モーション検知中か
  float movement() const { return movement_; };   // 最新の移動量スコア
  // 最後に動きを検知してからの経過時間(ms)。検知実績がなければ ULONG_MAX。
  unsigned long msSinceLastMotion() const;

  // 5秒ごとのステータス行を出力する。
  void logStatus(unsigned long now);

 private:
  static void onMotionCb(bool motion);   // ESPectre コールバック(動作検知の edge)
  static void onReadyCb(bool ready);
  static void onFaultCb(const char *message);

  // 複数AP対応の切断時リカバリ: 非同期スキャン → 最強の登録APへ接続
  enum class WifiRecovery { Idle, Scanning, Connecting };
  void tryReconnect(unsigned long now);

  bool wifi_connected_ = false;
  bool ready_ = false;
  bool in_motion_ = false;
  float movement_ = 0.0f;
  unsigned long last_motion_ms_ = 0;
  bool has_motion_ = false;
  unsigned long motion_count_ = 0;       // 動作検知 edge の回数(記録用)
  unsigned long last_wifi_retry_ms_ = 0;
  WifiRecovery recovery_ = WifiRecovery::Idle;
  unsigned long recovery_started_ms_ = 0;
};
