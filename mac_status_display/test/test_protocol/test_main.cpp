#include <unity.h>

#include <stdio.h>
#include <string.h>

#include "status_protocol.h"

void setUp() {}
void tearDown() {}

namespace {

void buildFrame(const char *payload, char *output, size_t outputSize) {
  const uint16_t crc = macstatus::crc16Ccitt(
      reinterpret_cast<const uint8_t *>(payload), strlen(payload));
  snprintf(output, outputSize, "$%s*%04X", payload, crc);
}

void test_crc_standard_vector() {
  const char *value = "123456789";
  TEST_ASSERT_EQUAL_HEX16(
      0x29B1,
      macstatus::crc16Ccitt(reinterpret_cast<const uint8_t *>(value), strlen(value)));
}

void test_valid_frame() {
  char line[macstatus::kMaximumFrameLength + 1];
  buildFrame("MSD3,42,123,876,730,1,1048576,4096,10,5", line, sizeof(line));
  macstatus::StatusFrame frame = {};
  TEST_ASSERT_TRUE(macstatus::parseStatusFrame(line, frame));
  TEST_ASSERT_EQUAL_UINT16(42, frame.sequence);
  TEST_ASSERT_EQUAL_UINT16(123, frame.cpuTenths);
  TEST_ASSERT_EQUAL_UINT16(876, frame.memoryTenths);
  TEST_ASSERT_EQUAL_INT16(730, frame.codexRemainingTenths);
  TEST_ASSERT_TRUE(frame.codexUsageStale);
  TEST_ASSERT_EQUAL_UINT32(1048576, frame.downloadBytesPerSecond);
  TEST_ASSERT_EQUAL_UINT32(4096, frame.uploadBytesPerSecond);
  TEST_ASSERT_EQUAL_UINT8(10, frame.brightnessPercent);
  TEST_ASSERT_EQUAL_UINT8(5, frame.offlineBrightnessPercent);
}

void test_missing_codex_usage() {
  char line[macstatus::kMaximumFrameLength + 1];
  buildFrame("MSD3,0,0,1000,-1,0,0,0,50,5", line, sizeof(line));
  macstatus::StatusFrame frame = {};
  TEST_ASSERT_TRUE(macstatus::parseStatusFrame(line, frame));
  TEST_ASSERT_EQUAL_INT16(macstatus::kMissingCodexUsage, frame.codexRemainingTenths);
  TEST_ASSERT_FALSE(frame.codexUsageStale);
}

void test_bad_crc_is_rejected_without_mutating_output() {
  char line[] = "$MSD3,42,123,876,730,0,1,2,50,5*0000";
  macstatus::StatusFrame frame = {7, 8, 9, 10, false, 11, 12, 13, 14};
  TEST_ASSERT_FALSE(macstatus::parseStatusFrame(line, frame));
  TEST_ASSERT_EQUAL_UINT16(7, frame.sequence);
  TEST_ASSERT_EQUAL_UINT32(11, frame.downloadBytesPerSecond);
}

void test_out_of_range_and_extra_fields_are_rejected() {
  char cpuLine[macstatus::kMaximumFrameLength + 1];
  char brightnessLine[macstatus::kMaximumFrameLength + 1];
  char extraLine[macstatus::kMaximumFrameLength + 1];
  buildFrame("MSD3,1,1001,20,500,0,1,2,50,5", cpuLine, sizeof(cpuLine));
  buildFrame("MSD3,1,10,20,500,0,1,2,101,5", brightnessLine, sizeof(brightnessLine));
  buildFrame("MSD3,1,10,20,500,0,1,2,50,5,3", extraLine, sizeof(extraLine));
  macstatus::StatusFrame frame = {};
  TEST_ASSERT_FALSE(macstatus::parseStatusFrame(cpuLine, frame));
  TEST_ASSERT_FALSE(macstatus::parseStatusFrame(brightnessLine, frame));
  TEST_ASSERT_FALSE(macstatus::parseStatusFrame(extraLine, frame));

  char oldVersionLine[macstatus::kMaximumFrameLength + 1];
  buildFrame("MSD2,1,10,20,500,1,2,50,5", oldVersionLine, sizeof(oldVersionLine));
  TEST_ASSERT_FALSE(macstatus::parseStatusFrame(oldVersionLine, frame));
}

}  // namespace

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_crc_standard_vector);
  RUN_TEST(test_valid_frame);
  RUN_TEST(test_missing_codex_usage);
  RUN_TEST(test_bad_crc_is_rejected_without_mutating_output);
  RUN_TEST(test_out_of_range_and_extra_fields_are_rejected);
  return UNITY_END();
}
