// LINE Simple Beacon の BLE Advertising データ生成 (Phase 2)。
// 仕様: https://github.com/line/line-simple-beacon (README.ja.md) に厳密に一致させる。
// Arduino 固有のAPIに依存しない純C++なので、公式サンプルのバイト列と
// ネイティブテストで突き合わせできる。
//
// 構成 (公式仕様どおり、Big-endian):
//   [Flags AD]         02 01 06
//   [16bit UUID AD]    03 03 6F FE            (0xFE6F)
//   [Service Data AD]  [len] 16 6F FE + LINE Simple Beacon Frame
//   [Non-significant]  0x00 埋め (計31オクテット)
//
// LINE Simple Beacon Frame (8〜20バイト, 全必須):
//   Frame Type(1B, 0x02) + HWID(5B) + Measured TxPower(1B, 0x7F固定) + Device Message(0〜13B)
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace linebeacon {

constexpr uint16_t kServiceUuid = 0xFE6F;
constexpr uint8_t kFrameType = 0x02;
constexpr uint8_t kMeasuredTxPower = 0x7F;  // 仕様: 0x7F のみ使用可
constexpr size_t kHwidBytes = 5;
constexpr size_t kMaxDeviceMessageBytes = 13;
constexpr size_t kAdvPayloadSize = 31;      // BLE 4.0 の advertising data 上限

// Service Data AD 部分のバイト数 (= AD長1 + AD type1 + UUID2 + フレーム長)
constexpr size_t kMaxServiceDataSize = 3 + kHwidBytes + 2 + kMaxDeviceMessageBytes;

// LINE Simple Beacon Frame 部分を構築する。
// dm_len>0: 7 + dm_len バイト。dm_len==0 (未指定): 仕様どおり Device Message に 0x00 を
// 入れて 8 バイト。
// 成功: 書き込んだバイト数 / 失敗(引数不正): 0
size_t buildFrame(const uint8_t *hwid,  // 5バイト
                  const uint8_t *dm,    // Device Message (0〜13バイト、dm_len==0 なら参照しない)
                  size_t dm_len, uint8_t *out, size_t out_cap);

// Advertising data 全体 (31バイト、末尾 0x00 埋め) を構築する。
// 成功: 31 / 失敗(引数不正): 0
size_t buildAdvertisingPayload(const uint8_t *hwid, const uint8_t *dm, size_t dm_len,
                               uint8_t *out, size_t out_cap);

// HWID の16進10文字 ("32AF519E88") を5バイトに変換する。成功: true
bool hwidFromHex(const char *hex, uint8_t hwid[kHwidBytes]);

// HEX文字列 ("0032006900...") をバイト列に変換する。成功: 書き込んだバイト数
// 失敗 / 長すぎる(>13バイト): 0。空文字列は dm_len=0 として 0 を返す。
size_t dmFromHex(const char *hex, uint8_t dm[kMaxDeviceMessageBytes]);

}  // namespace linebeacon
