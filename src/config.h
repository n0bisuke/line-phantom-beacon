// ピン番号・定数を1か所にまとめる (AGENTS.md コード方針)
#pragma once

// ---- Wi-Fi ----
// ESPectre が CSI を取得するため 2.4GHz 802.11n 必須。SSID/パスワードは secrets.h。
static const unsigned long kWifiConnectTimeoutMs = 20000UL;   // 初回接続の待ち時間
static const unsigned long kWifiRetryIntervalMs = 10000UL;    // 切断時の再接続試行間隔

// ---- ESPectre ----
// 検知しきい値・CSI取得レートはライブラリ既定値に任せる(Phase 1 では未調整)。
// キャリブレーションは設置後に静止状態で行われる(onReady 参照)。

// ---- シリアルログ ----
static const unsigned long kStatusLogIntervalMs = 5000UL;     // 定期ステータス表示間隔
static const unsigned long kSerialStartupDelayMs = 1000UL;    // 起動直後のシリアル安定待ち
static const constexpr uint32_t kSerialBaud = 115200U;
