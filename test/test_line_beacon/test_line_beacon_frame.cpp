// lib/line_beacon_frame のテスト。
// 期待値は公式仕様 https://github.com/line/line-simple-beacon README.ja.md の
// Advertising data example (Parameter Example 1 / 2) をそのまま写したバイト列。
#include <unity.h>

#include <stdio.h>
#include <string.h>

#include "line_beacon_frame.h"

using namespace linebeacon;

void setUp() {}
void tearDown() {}

namespace {
void expect31(const uint8_t *actual, const uint8_t *expected) {
  TEST_ASSERT_NOT_NULL(actual);
  for (size_t i = 0; i < 31; ++i) {
    char msg[24];
    snprintf(msg, sizeof(msg), "byte %zu", i);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(expected[i], actual[i], msg);
  }
}
}  // namespace

// 公式 Example 1: HWID=32AF519E88, DM=0032006900FAFFFF000199FF88 (13バイト, DM最大)
void test_official_example1() {
  const uint8_t hwid[5] = {0x32, 0xAF, 0x51, 0x9E, 0x88};
  const uint8_t dm[13] = {0x00, 0x32, 0x00, 0x69, 0x00, 0xFA, 0xFF,
                          0xFF, 0x00, 0x01, 0x99, 0xFF, 0x88};
  uint8_t adv[31];
  TEST_ASSERT_EQUAL(31u, buildAdvertisingPayload(hwid, dm, 13, adv, sizeof(adv)));

  const uint8_t expected[31] = {
      0x02, 0x01, 0x06,  // Flags
      0x03, 0x03, 0x6F, 0xFE,
      0x17, 0x16, 0x6F, 0xFE,  // Service Data, 長 0x17 (AD type+UUID+7+13)
      0x02,                    // Frame Type
      0x32, 0xAF, 0x51, 0x9E, 0x88,  // HWID
      0x7F,                          // Measured TxPower
      // Device Message (13バイト)
      0x00, 0x32, 0x00, 0x69, 0x00, 0xFA, 0xFF, 0xFF, 0x00, 0x01, 0x99, 0xFF, 0x88
  };
  expect31(adv, expected);
}

// 公式 Example 2: HWID=8ADFAF4326, DM=20170228 (4バイト, 22バイト以降は0x00)
void test_official_example2() {
  const uint8_t hwid[5] = {0x8A, 0xDF, 0xAF, 0x43, 0x26};
  const uint8_t dm[4] = {0x20, 0x17, 0x02, 0x28};
  uint8_t adv[31];
  TEST_ASSERT_EQUAL(31u, buildAdvertisingPayload(hwid, dm, 4, adv, sizeof(adv)));

  const uint8_t expected[31] = {
      0x02, 0x01, 0x06, 0x03, 0x03, 0x6F, 0xFE,
      0x0E, 0x16, 0x6F, 0xFE,  // 長 0x0E (AD type+UUID+7+4)
      0x02, 0x8A, 0xDF, 0xAF, 0x43, 0x26, 0x7F,
      0x20, 0x17, 0x02, 0x28,  // Device Message
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  expect31(adv, expected);
}

// Device Message 未使用: 仕様どおり 0x00 を 1 バイト入れる (フレーム 8 バイト)
void test_empty_dm_uses_zero_placeholder() {
  const uint8_t hwid[5] = {0x11, 0x22, 0x33, 0x44, 0x55};
  uint8_t frame[8 + 1];
  TEST_ASSERT_EQUAL(8u, buildFrame(hwid, nullptr, 0, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_HEX8(0x02, frame[0]);
  TEST_ASSERT_EQUAL_HEX8(0x11, frame[1]);
  TEST_ASSERT_EQUAL_HEX8(0x7F, frame[6]);
  TEST_ASSERT_EQUAL_HEX8(0x00, frame[7]);

  uint8_t adv[31];
  const uint8_t expected[31] = {0x02, 0x01, 0x06, 0x03, 0x03, 0x6F, 0xFE,
                                0x0B, 0x16, 0x6F, 0xFE, 0x02, 0x11, 0x22,
                                0x33, 0x44, 0x55, 0x7F, 0x00, 0x00, 0x00};
  buildAdvertisingPayload(hwid, nullptr, 0, adv, sizeof(adv));
  expect31(adv, expected);
}

// 13バイトを超える DM は拒否
void test_dm_too_long_rejected() {
  const uint8_t hwid[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
  const uint8_t dm[14] = {0};
  uint8_t adv[31];
  TEST_ASSERT_EQUAL(0u, buildAdvertisingPayload(hwid, dm, 14, adv, sizeof(adv)));
}

// バッファ不足
void test_small_out_buffer_rejected() {
  const uint8_t hwid[5] = {0};
  uint8_t adv[30];
  TEST_ASSERT_EQUAL(0u, buildAdvertisingPayload(hwid, nullptr, 0, adv, sizeof(adv)));
}

// HWID HEX パーサ
void test_hwid_from_hex() {
  uint8_t hwid[5];
  TEST_ASSERT_TRUE(hwidFromHex("32AF519E88", hwid));
  TEST_ASSERT_EQUAL_HEX8(0x32, hwid[0]);
  TEST_ASSERT_EQUAL_HEX8(0xAF, hwid[1]);
  TEST_ASSERT_EQUAL_HEX8(0x51, hwid[2]);
  TEST_ASSERT_EQUAL_HEX8(0x9E, hwid[3]);
  TEST_ASSERT_EQUAL_HEX8(0x88, hwid[4]);
  TEST_ASSERT_TRUE(hwidFromHex("32af519e88", hwid));  // 小文字
  TEST_ASSERT_TRUE(hwidFromHex("32 af 51 9e 88", hwid));  // 区切りあり
  TEST_ASSERT_FALSE(hwidFromHex("32AF519E8", hwid));   // 9文字
  TEST_ASSERT_FALSE(hwidFromHex("32AF519E881", hwid));  // 11文字
  TEST_ASSERT_FALSE(hwidFromHex("ZZAF519E88", hwid));   // 非HEX
  TEST_ASSERT_FALSE(hwidFromHex("", hwid));
  TEST_ASSERT_FALSE(hwidFromHex(nullptr, hwid));
}

// DM HEX パーサ
void test_dm_from_hex() {
  uint8_t dm[13];
  TEST_ASSERT_EQUAL(0u, dmFromHex("", dm));         // 空 = DM未指定
  TEST_ASSERT_EQUAL(0u, dmFromHex(nullptr, dm));    // 未設定 = DM未指定
  TEST_ASSERT_EQUAL(4u, dmFromHex("20170228", dm));
  TEST_ASSERT_EQUAL_HEX8(0x20, dm[0]);
  TEST_ASSERT_EQUAL_HEX8(0x28, dm[3]);
  TEST_ASSERT_EQUAL(13u, dmFromHex("0032006900FAFFFF000199FF88", dm));
  TEST_ASSERT_EQUAL_HEX8(0x88, dm[12]);
  TEST_ASSERT_EQUAL(0u, dmFromHex("20170228000", dm));  // 奇数文字
  TEST_ASSERT_EQUAL(0u, dmFromHex("0011223344556677889AABBCCDDE", dm));  // 14バイト超
  TEST_ASSERT_EQUAL(0u, dmFromHex("GG:HH", dm));  // 非HEX
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_official_example1);
  RUN_TEST(test_official_example2);
  RUN_TEST(test_empty_dm_uses_zero_placeholder);
  RUN_TEST(test_dm_too_long_rejected);
  RUN_TEST(test_small_out_buffer_rejected);
  RUN_TEST(test_hwid_from_hex);
  RUN_TEST(test_dm_from_hex);
  return UNITY_END();
}
