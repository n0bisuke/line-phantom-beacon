#include "line_beacon_frame.h"

namespace linebeacon {

size_t buildFrame(const uint8_t *hwid, const uint8_t *dm, size_t dm_len, uint8_t *out,
                  size_t out_cap) {
  if (hwid == nullptr || out == nullptr || dm_len > kMaxDeviceMessageBytes) {
    return 0;
  }
  const size_t frame_len = 7 + (dm_len > 0 ? dm_len : 1);  // DM 未指定は 0x00 を 1 バイト
  if (out_cap < frame_len) {
    return 0;
  }
  size_t i = 0;
  out[i++] = kFrameType;
  for (size_t k = 0; k < kHwidBytes; ++k) {
    out[i++] = hwid[k];
  }
  out[i++] = kMeasuredTxPower;
  if (dm_len > 0) {
    if (dm == nullptr) {
      return 0;
    }
    for (size_t k = 0; k < dm_len; ++k) {
      out[i++] = dm[k];
    }
  } else {
    // 仕様: Device Message を使用しない場合は 0x00 を指定する
    out[i++] = 0x00;
  }
  return i;
}

size_t buildAdvertisingPayload(const uint8_t *hwid, const uint8_t *dm, size_t dm_len,
                               uint8_t *out, size_t out_cap) {
  if (out == nullptr || out_cap < kAdvPayloadSize) {
    return 0;
  }
  if (dm_len > kMaxDeviceMessageBytes) {
    return 0;
  }
  for (size_t k = 0; k < kAdvPayloadSize; ++k) {
    out[k] = 0x00;
  }

  // Flags AD
  out[0] = 0x02;
  out[1] = 0x01;  // AD type: Flags
  out[2] = 0x06;  // LE General Discoverable + BR/EDR Not Supported

  // 16-bit Service UUID AD (リトルエンディアンで 0x6F, 0xFE)
  out[3] = 0x03;
  out[4] = 0x03;  // AD type: Complete list of 16-bit UUIDs
  out[5] = 0x6F;
  out[6] = 0xFE;

  // Service Data AD
  uint8_t frame[7 + kMaxDeviceMessageBytes];
  const size_t frame_len = buildFrame(hwid, dm, dm_len, frame, sizeof(frame));
  if (frame_len == 0) {
    return 0;
  }
  const size_t sd_len = 1 /*AD type*/ + 2 /*UUID*/ + frame_len;
  if (7 + sd_len > kAdvPayloadSize) {
    return 0;
  }
  out[7] = static_cast<uint8_t>(sd_len);
  out[8] = 0x16;  // AD type: Service Data
  out[9] = 0x6F;
  out[10] = 0xFE;
  for (size_t k = 0; k < frame_len; ++k) {
    out[11 + k] = frame[k];
  }
  return kAdvPayloadSize;
}

bool hwidFromHex(const char *hex, uint8_t hwid[kHwidBytes]) {
  if (hex == nullptr) {
    return false;
  }
  size_t n = 0;
  for (const char *p = hex; *p != '\0'; ++p) {
    if (*p == ' ' || *p == ':') {
      continue;
    }
    ++n;
  }
  if (n != kHwidBytes * 2) {
    return false;
  }
  uint8_t byte = 0;
  size_t count = 0;
  for (const char *p = hex; *p != '\0'; ++p) {
    if (*p == ' ' || *p == ':') {
      continue;
    }
    uint8_t v;
    if (*p >= '0' && *p <= '9') {
      v = static_cast<uint8_t>(*p - '0');
    } else if (*p >= 'a' && *p <= 'f') {
      v = static_cast<uint8_t>(*p - 'a' + 10);
    } else if (*p >= 'A' && *p <= 'F') {
      v = static_cast<uint8_t>(*p - 'A' + 10);
    } else {
      return false;
    }
    byte = static_cast<uint8_t>((byte << 4) | v);
    if (++count % 2 == 0) {
      hwid[count / 2 - 1] = byte;
    }
  }
  return true;
}

size_t dmFromHex(const char *hex, uint8_t dm[kMaxDeviceMessageBytes]) {
  if (hex == nullptr) {
    return 0;
  }
  uint8_t parsed[kMaxDeviceMessageBytes];
  size_t count = 0;
  for (const char *p = hex; *p != '\0'; ++p) {
    if (*p == ' ' || *p == ':') {
      continue;
    }
    uint8_t v;
    if (*p >= '0' && *p <= '9') {
      v = static_cast<uint8_t>(*p - '0');
    } else if (*p >= 'a' && *p <= 'f') {
      v = static_cast<uint8_t>(*p - 'a' + 10);
    } else if (*p >= 'A' && *p <= 'F') {
      v = static_cast<uint8_t>(*p - 'A' + 10);
    } else {
      return 0;
    }
    if (count == kMaxDeviceMessageBytes * 2) {
      return 0;  // 長すぎる
    }
    if (count % 2 == 0) {
      parsed[count / 2] = static_cast<uint8_t>(v << 4);
    } else {
      parsed[count / 2] |= v;
    }
    ++count;
  }
  if (count == 0) {
    return 0;  // 空は dm 未指定扱い
  }
  if (count % 2 != 0) {
    return 0;  // 奇数文字は不正
  }
  for (size_t k = 0; k < count / 2; ++k) {
    dm[k] = parsed[k];
  }
  return count / 2;
}

}  // namespace linebeacon
