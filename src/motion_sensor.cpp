#include "motion_sensor.h"

#include <ESPectre.h>
#include <WiFi.h>

#include "config.h"
#include "secrets.h"

namespace {
ESPectre s_sensor;         // ライブラリの使用例に合わせ 1 インスタンス
MotionSensor *s_self;      // コールバック用(1 インスタンス運用)

// 登録 AP リスト(secrets.h の WIFI_CREDENTIALS マクロ)。空 SSID 行は実行時にスキップ。
struct WifiCredential {
  const char *ssid;
  const char *password;
};
constexpr WifiCredential kWifiCredentials[] = {WIFI_CREDENTIALS};
constexpr size_t kWifiCredentialCount = sizeof(kWifiCredentials) / sizeof(kWifiCredentials[0]);
}  // namespace

void MotionSensor::begin() {
  s_self = this;

  WiFi.mode(WIFI_STA);

  // 登録 AP をスキャンして最も強いものに接続(起動時のみのブロック)。
  Serial.printf("[wifi] %d APs registered, scanning...\n", (int)kWifiCredentialCount);
  const int found = WiFi.scanNetworks();
  const char *best_ssid = nullptr;
  const char *best_pass = nullptr;
  int best_rssi = -200;
  for (int i = 0; i < found; i++) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;  // 隠し SSID は登録と比較できない
    for (size_t c = 0; c < kWifiCredentialCount; c++) {
      if (kWifiCredentials[c].ssid[0] != '\0' && ssid.equals(kWifiCredentials[c].ssid) &&
          WiFi.RSSI(i) > best_rssi) {
        best_rssi = WiFi.RSSI(i);
        best_ssid = kWifiCredentials[c].ssid;
        best_pass = kWifiCredentials[c].password;
      }
    }
  }
  WiFi.scanDelete();
  if (best_ssid == nullptr) {
    // 見つからない場合(隠れSSID/一時的に不在)は最初の有効な登録で試す
    for (size_t c = 0; c < kWifiCredentialCount; c++) {
      if (kWifiCredentials[c].ssid[0] != '\0') {
        best_ssid = kWifiCredentials[c].ssid;
        best_pass = kWifiCredentials[c].password;
        break;
      }
    }
    Serial.println("[wifi] スキャンで見つからず、最初の登録で接続を試みます");
  }

  if (best_ssid != nullptr) {
    WiFi.begin(best_ssid, best_pass);
  }

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

  // motion 状態(m==true)の間は最終検知時刻を更新し続ける。
  // ESPectre は物理的な動きが止まっても motion 状態を数秒ホールドするため、
  // エッジ時刻だけで計ると「画面の秒数」と「疑似イベントの10秒計測」がズレる。
  // 状態ベースで更新すれば idle 計測は「motion 解除からの経過」に統一され、
  // LCD 表示と push タイミングが一致する(解除+10秒で疑似leave)。
  if (in_motion_) {
    last_motion_ms_ = millis();
    has_motion_ = true;
  }

  const bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected != wifi_connected_) {
    wifi_connected_ = connected;
    Serial.println(connected ? "[wifi] connected" : "[wifi] disconnected");
    if (connected) {
      recovery_ = WifiRecovery::Idle;  // 接続が戻ったらリカバリ進行中の状態を解消
    }
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
  switch (recovery_) {
    case WifiRecovery::Idle:
      // 切断してから一定間隔で非同期スキャンを開始
      if (millis() - last_wifi_retry_ms_ < kWifiRetryIntervalMs) {
        return;
      }
      last_wifi_retry_ms_ = millis();
      WiFi.scanNetworks(true /* async */);
      recovery_ = WifiRecovery::Scanning;
      recovery_started_ms_ = now;
      Serial.println("[wifi] scanning for registered APs...");
      return;

    case WifiRecovery::Scanning: {
      const int32_t state = WiFi.scanComplete();
      if (state == WIFI_SCAN_RUNNING || state == WIFI_SCAN_FAILED) {
        // スキャン継続(タイムアウトはライブラリ側で一定時間後に完結する)
        if (now - recovery_started_ms_ > kWifiConnectTimeoutMs) {
          WiFi.scanDelete();
          recovery_ = WifiRecovery::Idle;
          Serial.println("[wifi] scan timeout");
        }
        return;
      }
      if (state <= 0) {  // 0件 / エラー
        Serial.println("[wifi] 登録APが見つかりません(リトライします)");
        recovery_ = WifiRecovery::Idle;
        return;
      }
      // 最強の登録 AP を選ぶ
      const char *best_ssid = nullptr;
      const char *best_pass = nullptr;
      int best_rssi = -200;
      for (int i = 0; i < state; i++) {
        const String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        for (size_t c = 0; c < kWifiCredentialCount; c++) {
          if (kWifiCredentials[c].ssid[0] != '\0' && ssid.equals(kWifiCredentials[c].ssid) &&
              WiFi.RSSI(i) > best_rssi) {
            best_rssi = WiFi.RSSI(i);
            best_ssid = kWifiCredentials[c].ssid;
            best_pass = kWifiCredentials[c].password;
          }
        }
      }
      WiFi.scanDelete();
      if (best_ssid == nullptr) {
        Serial.println("[wifi] 登録APが見つかりません(リトライします)");
        recovery_ = WifiRecovery::Idle;
        return;
      }
      Serial.print("[wifi] switch AP: ");
      Serial.println(best_ssid);
      WiFi.begin(best_ssid, best_pass);
      recovery_ = WifiRecovery::Connecting;
      recovery_started_ms_ = now;
      return;
    }

    case WifiRecovery::Connecting:
      // loop() が接続を検知したら Idle に戻る。タイムアウトしたら一旦諦める
      if (WiFi.status() == WL_CONNECTED) {
        recovery_ = WifiRecovery::Idle;
        return;
      }
      if (now - recovery_started_ms_ > kWifiConnectTimeoutMs) {
        WiFi.disconnect();
        recovery_ = WifiRecovery::Idle;
        last_wifi_retry_ms_ = now;
        Serial.println("[wifi] connect failed (リトライします)");
      }
      return;
  }
}

void MotionSensor::onMotionCb(bool motion) {
  if (motion) {
    s_self->last_motion_ms_ = millis();
    s_self->has_motion_ = true;
    s_self->motion_count_++;
    // 動作検知時刻を記録(Phase 1 ではシリアルログが一次記録)
    // 注: main.cpp の [event] は疑似イベント状態機(遷移時)のログ。こちらは ESPectre の生の edge
    Serial.printf("[motion] edge (%lu)\n", millis());
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
