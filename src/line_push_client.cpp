#include "line_push_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <WiFi.h>

#include "config.h"
#include "line_root_ca.h"
#include "secrets.h"

namespace {
// 通知テキスト(完全に独自の疑似イベント。公式の leave イベントを偽装しない)
// push 文言。秒数は secrets.h のしきい値から自動で付く(しきい値を変えても文言が追従)。
String makeText(const char *type) {
  if (strcmp(type, "motion") == 0) {
    return String("\xF0\x9F\x8F\x83 動きを検知しました(擬似)");
  }
  if (strcmp(type, "pseudo_leave") == 0) {
    return String("\xF0\x9F\x9A\xAA 動きがなくなりました(") + (PSEUDO_LEAVE_MS / 1000) + "秒無動作・擬似)";
  }
  if (strcmp(type, "pseudo_leave_continued") == 0) {
    return String("\xF0\x9F\x9A\xAA 継続して動きがありません(") + (PSEUDO_CONTINUED_MS / 1000) + "秒無動作・擬似)";
  }
  if (strcmp(type, "sleep_suspected") == 0) {
    return String("\xF0\x9F\x98\xB4 寝落ちの可能性(") + (SLEEP_SUSPECTED_MS / 1000) + "秒無動作・擬似)";
  }
  return String("\xE2\x9D\x93 unknown event: ") + type;
}

// 同一 type の再送抑制(minIntervalByType: 動き系は短め、静止系は長め)
struct LastSent {
  const char *type;
  unsigned long min_interval_ms;
  unsigned long last_ms;
};
LastSent s_last[] = {
    {"motion", kLineMotionIntervalMs, 0},
    {"pseudo_leave", kLineQuietIntervalMs, 0},
    {"pseudo_leave_continued", kLineQuietIntervalMs, 0},
    {"sleep_suspected", kLineQuietIntervalMs, 0},
};

String makeBody(const String &text) {
  String body = "{\"to\":\"";
  body += LINE_USER_ID;
  body += "\",\"messages\":[{\"type\":\"text\",\"text\":\"";
  body += text;
  body += "(";
  body += DEVICE_ID;
  body += ")\"}]}";
  return body;
}
}  // namespace

void LinePushClient::begin() {
  Serial.println("[line] direct push (api.line.me, サーバーレス構成)");
  if (sizeof(LINE_CHANNEL_TOKEN) <= 1) {
    Serial.println("[line] 警告: LINE_CHANNEL_TOKEN が未設定");
  }
}

bool LinePushClient::push(const char *type, unsigned long now) {
  if (strcmp(type, "motion_resumed") == 0) {
    type = "motion";  // 再開も「動き検知」と同じ通知
  }

  for (LastSent &entry : s_last) {
    if (strcmp(entry.type, type) == 0) {
      if (entry.last_ms != 0 && now - entry.last_ms < entry.min_interval_ms) {
        Serial.printf("[line] suppressed %s (min interval)\n", type);
        return true;  // イベントとしては処理済み扱い
      }
      entry.last_ms = now;
      break;
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[line] skipped (Wi-Fi 未接続)");
    return false;
  }

  WiFiClientSecure tls;
  tls.setCACert(LINE_ROOT_CA_PEM);  // 公開CAで検証(setInsecure は使わない)
  HTTPClient http;
  http.setConnectTimeout(kLineTimeoutMs);
  http.setTimeout(kLineTimeoutMs);
  if (!http.begin(tls, "https://api.line.me/v2/bot/message/push")) {
    Serial.println("[line] http.begin() failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + LINE_CHANNEL_TOKEN);

  const int code = http.POST(makeBody(makeText(type)));
  http.end();

  if (code == 200) {
    Serial.printf("[line] pushed %s\n", type);
    return true;
  }
  Serial.printf("[line] push %s failed: %d%s\n", type, code,
                code == 401 ? " (トークン失効?)" : code < 0 ? " (TLS 接続失敗?)" : "");
  return false;
}
