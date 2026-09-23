#include <unity.h>

#include <stdio.h>
#include <string.h>

#include "status_protocol.h"
#include "offline_clock.h"

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

void test_clock_crc_bounds_and_no_output_mutation() {
  char line[32];
  uint32_t seconds = 10;
  buildFrame("MSC1,86399", line, sizeof(line));
  TEST_ASSERT_TRUE(macstatus::parseClockFrame(line, seconds));
  TEST_ASSERT_EQUAL_UINT32(86399, seconds);
  buildFrame("MSC1,86400", line, sizeof(line));
  TEST_ASSERT_FALSE(macstatus::parseClockFrame(line, seconds));
  TEST_ASSERT_EQUAL_UINT32(86399, seconds);
  TEST_ASSERT_FALSE(macstatus::parseClockFrame("$MSC1,1*0000", seconds));
  buildFrame("MSC1,+1", line, sizeof(line));
  TEST_ASSERT_FALSE(macstatus::parseClockFrame(line, seconds));
  buildFrame("MSC1,0", line, sizeof(line));
  TEST_ASSERT_TRUE(macstatus::parseClockFrame(line, seconds));
  TEST_ASSERT_EQUAL_UINT32(0, seconds);
}

void test_offline_clock_midnight_wrap_and_resync() {
  macstatus::OfflineClock clock;
  TEST_ASSERT_FALSE(clock.valid());
  clock.tick(10000);
  TEST_ASSERT_FALSE(clock.valid());
  clock.sync(86399, UINT32_MAX - 499);
  clock.tick(0);
  TEST_ASSERT_EQUAL_UINT32(86399, clock.seconds());
  clock.tick(500);
  TEST_ASSERT_EQUAL_UINT32(0, clock.seconds());
  clock.tick(86401500);
  TEST_ASSERT_EQUAL_UINT32(1, clock.seconds());
  clock.sync(43200, 123);
  clock.tick(1123);
  TEST_ASSERT_EQUAL_UINT32(43201, clock.seconds());
}

void test_calendar_bounds_and_date_rollover() {
  char line[40];
  uint32_t epoch = 7;
  buildFrame("MSC2,1790207999", line, sizeof(line));
  TEST_ASSERT_TRUE(macstatus::parseCalendarFrame(line, epoch));
  macstatus::OfflineClock clock;
  clock.syncEpoch(epoch, UINT32_MAX - 499);
  clock.tick(500);
  TEST_ASSERT_TRUE(clock.dateValid());
  TEST_ASSERT_EQUAL_UINT32(epoch + 1, clock.epoch());
  TEST_ASSERT_EQUAL_UINT32(0, clock.seconds());
  buildFrame("MSC2,4102444800", line, sizeof(line));
  TEST_ASSERT_FALSE(macstatus::parseCalendarFrame(line, epoch));
  TEST_ASSERT_EQUAL_UINT32(1790207999, epoch);
  buildFrame("MSC2,+790207999", line, sizeof(line));
  TEST_ASSERT_FALSE(macstatus::parseCalendarFrame(line, epoch));
  TEST_ASSERT_FALSE(macstatus::validAuxFrame("$MSW1,{}*0000", "$MSW1,"));
}

void test_valid_frame() {
  char line[macstatus::kMaximumFrameLength + 1];
  buildFrame("MSD4,42,123,876,490,610,730,1,1048576,4096,KR-SE,0,10,5", line,
             sizeof(line));
  macstatus::StatusFrame frame = {};
  TEST_ASSERT_TRUE(macstatus::parseStatusFrame(line, frame));
  TEST_ASSERT_EQUAL_UINT16(42, frame.sequence);
  TEST_ASSERT_EQUAL_UINT16(123, frame.cpuTenths);
  TEST_ASSERT_EQUAL_UINT16(876, frame.memoryTenths);
  TEST_ASSERT_EQUAL_INT16(490, frame.cpuTemperatureTenths);
  TEST_ASSERT_EQUAL_INT16(610, frame.gpuTemperatureTenths);
  TEST_ASSERT_EQUAL_INT16(730, frame.codexRemainingTenths);
  TEST_ASSERT_TRUE(frame.codexUsageStale);
  TEST_ASSERT_EQUAL_UINT32(1048576, frame.downloadBytesPerSecond);
  TEST_ASSERT_EQUAL_UINT32(4096, frame.uploadBytesPerSecond);
  TEST_ASSERT_EQUAL_STRING("KR-SE", frame.networkLocation);
  TEST_ASSERT_FALSE(frame.networkLocationStale);
  TEST_ASSERT_EQUAL_UINT8(10, frame.brightnessPercent);
  TEST_ASSERT_EQUAL_UINT8(5, frame.offlineBrightnessPercent);
}

void test_missing_codex_usage() {
  char line[macstatus::kMaximumFrameLength + 1];
  buildFrame("MSD4,0,0,1000,-1,-1,-1,0,0,0,--,0,50,5", line,
             sizeof(line));
  macstatus::StatusFrame frame = {};
  TEST_ASSERT_TRUE(macstatus::parseStatusFrame(line, frame));
  TEST_ASSERT_EQUAL_INT16(macstatus::kMissingCodexUsage, frame.codexRemainingTenths);
  TEST_ASSERT_EQUAL_INT16(macstatus::kMissingTemperature, frame.cpuTemperatureTenths);
  TEST_ASSERT_EQUAL_INT16(macstatus::kMissingTemperature, frame.gpuTemperatureTenths);
  TEST_ASSERT_EQUAL_STRING("--", frame.networkLocation);
  TEST_ASSERT_FALSE(frame.codexUsageStale);
}

void test_legacy_msd3_frame_maps_missing_temperatures() {
  char line[macstatus::kMaximumFrameLength + 1];
  buildFrame("MSD3,42,123,876,730,0,1048576,4096,50,5", line, sizeof(line));
  macstatus::StatusFrame frame = {};
  TEST_ASSERT_TRUE(macstatus::parseStatusFrame(line, frame));
  TEST_ASSERT_EQUAL_UINT16(123, frame.cpuTenths);
  TEST_ASSERT_EQUAL_INT16(macstatus::kMissingTemperature, frame.cpuTemperatureTenths);
  TEST_ASSERT_EQUAL_INT16(macstatus::kMissingTemperature, frame.gpuTemperatureTenths);
  TEST_ASSERT_EQUAL_INT16(730, frame.codexRemainingTenths);
  TEST_ASSERT_EQUAL_STRING("--", frame.networkLocation);
}

void test_bad_crc_is_rejected_without_mutating_output() {
  char line[] = "$MSD3,42,123,876,730,0,1,2,50,5*0000";
  macstatus::StatusFrame frame = {};
  frame.sequence = 7;
  frame.downloadBytesPerSecond = 13;
  TEST_ASSERT_FALSE(macstatus::parseStatusFrame(line, frame));
  TEST_ASSERT_EQUAL_UINT16(7, frame.sequence);
  TEST_ASSERT_EQUAL_UINT32(13, frame.downloadBytesPerSecond);
}

void test_out_of_range_and_extra_fields_are_rejected() {
  char cpuLine[macstatus::kMaximumFrameLength + 1];
  char brightnessLine[macstatus::kMaximumFrameLength + 1];
  char locationLine[macstatus::kMaximumFrameLength + 1];
  char extraLine[macstatus::kMaximumFrameLength + 1];
  buildFrame("MSD4,1,1001,20,400,500,500,0,1,2,CN-SH,0,50,5", cpuLine,
             sizeof(cpuLine));
  buildFrame("MSD4,1,10,20,400,500,500,0,1,2,CN-SH,0,101,5", brightnessLine,
             sizeof(brightnessLine));
  buildFrame("MSD4,1,10,20,400,500,500,0,1,2,cn-sh,0,50,5", locationLine,
             sizeof(locationLine));
  buildFrame("MSD4,1,10,20,400,500,500,0,1,2,CN-SH,0,50,5,3", extraLine,
             sizeof(extraLine));
  macstatus::StatusFrame frame = {};
  TEST_ASSERT_FALSE(macstatus::parseStatusFrame(cpuLine, frame));
  TEST_ASSERT_FALSE(macstatus::parseStatusFrame(brightnessLine, frame));
  TEST_ASSERT_FALSE(macstatus::parseStatusFrame(locationLine, frame));
  TEST_ASSERT_FALSE(macstatus::parseStatusFrame(extraLine, frame));

  char oldVersionLine[macstatus::kMaximumFrameLength + 1];
  buildFrame("MSD2,1,10,20,500,1,2,50,5", oldVersionLine, sizeof(oldVersionLine));
  TEST_ASSERT_FALSE(macstatus::parseStatusFrame(oldVersionLine, frame));
}

}  // namespace

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_crc_standard_vector);
  RUN_TEST(test_clock_crc_bounds_and_no_output_mutation);
  RUN_TEST(test_offline_clock_midnight_wrap_and_resync);
  RUN_TEST(test_calendar_bounds_and_date_rollover);
  RUN_TEST(test_valid_frame);
  RUN_TEST(test_missing_codex_usage);
  RUN_TEST(test_legacy_msd3_frame_maps_missing_temperatures);
  RUN_TEST(test_bad_crc_is_rejected_without_mutating_output);
  RUN_TEST(test_out_of_range_and_extra_fields_are_rejected);
  return UNITY_END();
}
