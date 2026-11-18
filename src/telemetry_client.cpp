#include "telemetry_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "telemetry_root_ca.h"
#include "device_label.h"
#include "secrets.h"

// テレメトリはオプション機能: secrets.h に定義がなければ完全に無効(既定値は空文字)
#if !defined(TELEMETRY_URL)
#define TELEMETRY_URL ""
#define TELEMETRY_LOKI_USER ""
#define TELEMETRY_TOKEN ""
#endif

namespace {

// 有効判定はトークンのみ。token ありで URL/USER が未設定の場合は begin() で
// 分かりやすい案内を出す(URL とインスタンス ID はアカウント固有で Secrets.h に必須)。
bool lokiConfigured() {
  return strlen(TELEMETRY_TOKEN) > 0;
}

bool lokiFullyConfigured() {
  return strlen(TELEMETRY_URL) > 0 && strlen(TELEMETRY_LOKI_USER) > 0;
}

// Loki push の共通送信。成功は 204 (No Content)。Wi-Fi 未接続時は静かに諦める。
bool postLokiBody(const String &body) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  WiFiClientSecure tls;
  tls.setCACert(TELEMETRY_ROOT_CA_PEM);  // 公開 CA で検証(setInsecure は使わない)
  HTTPClient http;
  http.setConnectTimeout(kLineTimeoutMs);
  http.setTimeout(kLineTimeoutMs);
  if (!http.begin(tls, TELEMETRY_URL)) {
    Serial.println("[telemetry] http.begin() failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  if (strlen(TELEMETRY_LOKI_USER) > 0) {
    http.setAuthorization(TELEMETRY_LOKI_USER, TELEMETRY_TOKEN);
  } else {
    http.addHeader("Authorization", String("Bearer ") + TELEMETRY_TOKEN);
  }
  const int code = http.POST(body);
  http.end();

  if (code == 204 || code == 200) {
    return true;
  }
  Serial.printf("[telemetry] loki push failed: %d%s\n", code,
                code == 401 ? " (認証失敗? USER/TOKEN を確認)"
                            : code == 400 ? " (ペイロード/時刻を確認)"
                                          : "");
  return false;
}

// 1 リクエストに複数ストレージ(kind=label)を詰めて送る Loki ペイロードを組む
String buildStreamsPayload(const char *device_label, int64_t ns,
                           const char *kind1, const String &value1,
                           const char *kind2 = nullptr, const String &value2 = String()) {
  String body = "{\"streams\":[";
  body += "{\"stream\":{\"device\":\"";
  body += device_label;
  body += "\",\"kind\":\"";
  body += kind1;
  body += "\"},\"values\":[[\"";
  body += String((unsigned long long)ns);
  body += "\",\"";
  body += value1;
  body += "\"]]}";
  if (kind2 != nullptr) {
    body += ",{\"stream\":{\"device\":\"";
    body += device_label;
    body += "\",\"kind\":\"";
    body += kind2;
    body += "\"},\"values\":[[\"";
    body += String((unsigned long long)ns);
    body += "\",\"";
    body += value2;
    body += "\"]]}";
  }
  body += "]}";
  return body;
}

}  // namespace

void TelemetryClient::begin() {
  if (!lokiConfigured()) {
    Serial.println("[telemetry] disabled (TELEMETRY_TOKEN 未設定)");
    return;
  }
  if (!lokiFullyConfigured()) {
    Serial.println("[telemetry] TELEMETRY_TOKEN あり。あと URL と USER も secrets.h に設定すると有効");
    Serial.println("         TELEMETRY_URL=https://logs-prod-XXX.grafana.net/loki/api/v1/push");
    Serial.println("         TELEMETRY_USER=<Loki のインスタンス ID(数字)>");
    return;
  }
  Serial.println("[telemetry] Loki push enabled (Grafana Cloud)");
}

// SNTP 同期(初回のみ)。Loki はタイムスタンプが必須のため、Wi-Fi 接続後に一度だけ同期する。
bool TelemetryClient::syncTimeOnce() {
  if (sntp_synced_) {
    return true;
  }
  if (!sntp_started_ && WiFi.status() == WL_CONNECTED) {
    configTime(9 * 3600, 0, "ntp.nict.jp", "time.google.com");  // JST(+9)、国産NTPを第一候補に
    sntp_started_ = true;
    Serial.println("[telemetry] SNTP sync started");
  }
  if (sntp_started_ && time(nullptr) > 1600000000UL) {  // 概ね 2020 年以降なら同期済み扱い
    sntp_synced_ = true;
    Serial.println("[telemetry] time synced");
  }
  return sntp_synced_;
}

bool TelemetryClient::pushEvent(const char *type, const char *device_label, unsigned long now) {
  if (!lokiFullyConfigured()) {
    return false;
  }
  if (!syncTimeOnce()) {
    Serial.println("[telemetry] skipped (時刻未同期)");
    return false;
  }
  const int64_t ns = (int64_t)time(nullptr) * 1000000000LL;

  // イベント行と、在室フラグ(presence)の遷移を 1 リクエストで送る。
  // motion / motion_resumed → presence=1、leave/continued/sleep → presence=0。
  String value = "event=";
  value += type;
  value += " (";
  value += device_label;
  value += ")";
  const bool occupied = (strcmp(type, "motion") == 0 || strcmp(type, "motion_resumed") == 0);
  String presence = "presence=";
  presence += occupied ? "1" : "0";
  presence += " (";
  presence += device_label;
  presence += ")";

  const String body =
      buildStreamsPayload(device_label, ns, "event", value, "presence", presence);
  return postLokiBody(body);
}

void TelemetryClient::heartbeat(unsigned long now, const char *device_label, bool wifi,
                                bool ready) {
  if (!lokiFullyConfigured()) {
    return;
  }
  if (s_last_heartbeat_ms_ != 0 && now - s_last_heartbeat_ms_ < kTelemetryHeartbeatIntervalMs) {
    return;
  }
  if (!syncTimeOnce()) {
    return;
  }
  s_last_heartbeat_ms_ = now;
  String value = "status wifi=";
  value += wifi ? "on" : "off";
  value += " ready=";
  value += ready ? "1" : "0";
  value += " uptime_h=";
  value += String(now / 3600000UL, 1);
  value += " (";
  value += device_label;
  value += ")";
  if (!syncTimeOnce()) {
    return;
  }
  const int64_t ns = (int64_t)time(nullptr) * 1000000000LL;
  const String body = buildStreamsPayload(device_label, ns, "status", value);
  postLokiBody(body);
}
