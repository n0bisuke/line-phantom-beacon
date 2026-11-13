// LINE Messaging API へ ESP32 から直接 push するクライアント(Phase 5・サーバーレス構成)。
// チャネルアクセストークンは include/secrets.h に置き、Git にはコミットしない。
// api.line.me は TLS 証明書検証あり(公開CA証明書 src/line_root_ca.h)。
#pragma once

#include <Arduino.h>

class LinePushClient {
 public:
  void begin();  // 使用モードの表示
  // type: "motion" / "pseudo_leave" / "sleep_suspected" / "motion_resumed"。now は millis()。
  // 同一 type は一定間隔内の再送を抑制する(LINE の無料メッセージ枠保護・重複抑制)。
  bool push(const char *type, unsigned long now);
};
