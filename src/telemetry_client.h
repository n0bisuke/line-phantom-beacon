// Grafana Cloud Loki へのテレメトリ送信(サーバーレス: ESP32 から直接 HTTPS POST)。
// - pushEvent(): 疑似イベント(motion/leave/...)を 1 行ログとして送る(LINE とは独立。
//   LINE の月200通制限に達して 429 になっても、こちらには全記録が残る)
// - heartbeat(): 5 分ごとに wifi/ready/uptime の状態ログを送る(稼働監視の隙間検出用)
// secrets.h の TELEMETRY_LOKI_URL が空文字なら、このクライアントは完全に無効(送信しない)。
#pragma once

#include <Arduino.h>

class TelemetryClient {
 public:
  void begin();
  // イベントを Loki に送る。Loki に時刻(NTP)が要るため、未同期時は同期を試みて失敗なら false。
  bool pushEvent(const char *type, const char *device_label, unsigned long now);
  // 状態の heartbeat。内部で最小間隔(kTelemetryHeartbeatIntervalMs)を管理する。
  void heartbeat(unsigned long now, const char *device_label, bool wifi, bool ready);

 private:
  bool syncTimeOnce();
  bool postLoki(const char *device_label, const char *stream_kind, const String &value,
                unsigned long now);

  bool sntp_started_ = false;
  bool sntp_synced_ = false;
  unsigned long s_last_heartbeat_ms_ = 0;
  bool s_disabled_printed_ = false;
};
