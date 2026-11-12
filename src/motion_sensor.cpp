#include "motion_sensor.h"

#include <ESPectre.h>
#include <WiFi.h>

#include "config.h"
#include "secrets.h"

namespace {
ESPectre s_sensor;         // ライブラリの使用例に合わせ 1 インスタンス
MotionSensor *s_self;      // コールバック用(1 インスタンス運用)
}  // namespace

void MotionSensor::begin() {
  s_self = this;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // 起動時のみ待つ(以後は loop() 内でノンブロッキング再接続)
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiConnectTimeoutMs) {
    delay(50);
  }
  wifi_connected_ = (WiFi.status() == WL_CONNECTED);
  if (wifi_connected_) {
    Serial.print("[wifi] connected");
    Serial.print(" channel=");
    Serial.print(WiFi.channel());
    Serial.print(" rssi=");
    Serial.println(WiFi.RSSI());
  } else {
    Serial.println("[wifi] connect failed (loop 内で再試行します)");
  }

  s_sensor.onReady(onReadyCb);
  s_sensor.onMotion(onMotionCb);
  s_sensor.onFault(onFaultCb);

  // トラフィック生成: 既定の PING(ICMP)は ICMP を弾く AP(iPhone ホットスポット等)で
  // CSI が流れなくなるため、wifi_raw(Null Data フレーム + ACK)に切り替える。
  s_sensor.config().traffic_generator_mode = espectre::TrafficGeneratorMode::WIFI_RAW;
  Serial.println("[espectre] traffic=wifi_raw (Null Data + ACK)");

  if (!s_sensor.begin()) {
    Serial.println("[espectre] begin() failed (Wi-Fi STA 必須)");
  }
}

void MotionSensor::loop() {
  s_sensor.loop();

  const bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected != wifi_connected_) {
    wifi_connected_ = connected;
    Serial.println(connected ? "[wifi] connected" : "[wifi] disconnected");
  }
  if (!connected) {
    tryReconnect(millis());
  }
}

unsigned long MotionSensor::msSinceLastMotion() const {
  if (!has_motion_) {
    return ULONG_MAX;
  }
  return millis() - last_motion_ms_;
}

void MotionSensor::logStatus(unsigned long now) {
  const unsigned long idle = msSinceLastMotion();
  Serial.printf("[status] wifi=%s ready=%d motion=%d movement=%.1f idle_ms=%s count=%lu up_ms=%lu\n",
                wifi_connected_ ? "on" : "off",
                ready_ ? 1 : 0,
                in_motion_ ? 1 : 0,
                movement_,
                idle == ULONG_MAX ? "none" : String((unsigned long)idle).c_str(),
                motion_count_,
                now);
  // CSI パイプラインの診断(1秒あたりのレート)。CSI が流れていない場合の切り分け用。
  const auto *diag = s_sensor.runtime().diagnostics_sample();
  if (diag != nullptr) {
    Serial.printf("[csi] gen_pps=%.0f rx_pps=%.0f accepted_pps=%.0f admitted_pps=%.0f occ=%.2f\n",
                  diag->generator_pps,
                  diag->traffic_rx_pps,
                  diag->csi_accepted_pps,
                  diag->csi_admitted_pps,
                  diag->csi_occupancy_ratio);
  }
}

void MotionSensor::tryReconnect(unsigned long now) {
  if (now - last_wifi_retry_ms_ < kWifiRetryIntervalMs) {
    return;
  }
  last_wifi_retry_ms_ = now;
  Serial.println("[wifi] reconnecting...");
  WiFi.reconnect();
}

void MotionSensor::onMotionCb(bool motion) {
  if (motion) {
    s_self->last_motion_ms_ = millis();
    s_self->has_motion_ = true;
    s_self->motion_count_++;
    // 動作検知時刻を記録(Phase 1 ではシリアルログが一次記録)
    Serial.printf("[event] motion (%lu)\n", millis());
  }
  s_self->in_motion_ = motion;
}

void MotionSensor::onReadyCb(bool ready) {
  s_self->ready_ = ready;
  Serial.println(ready ? "[espectre] ready (キャリブレーション完了)"
                       : "[espectre] waiting (Wi-Fi接続とキャリブレーション待ち)");
}

void MotionSensor::onFaultCb(const char *message) {
  Serial.printf("[espectre] fault: %s\n", message);
}
