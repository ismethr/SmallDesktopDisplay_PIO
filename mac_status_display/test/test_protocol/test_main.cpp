#include <unity.h>

#include <stdio.h>
#include <string.h>

#include "status_protocol.h"
#include "offline_clock.h"
#include "ai_usage_protocol.h"
#include "line_reader.h"
#include "status_format.h"

void setUp() {}
void tearDown() {}

namespace {

void buildFrame(const char *payload, char *output, size_t outputSize) {
  const uint16_t crc = macstatus::crc16Ccitt(
      reinterpret_cast<const uint8_t *>(payload), strlen(payload));
  snprintf(output, outputSize, "$%s*%04X", payload, crc);
}

void test_claude_aux_frame_validation() {
  macstatus::ClaudeUsageFrame frame;
  char line[80];
  buildFrame("MSA1,0,1000,0", line, sizeof(line));
  TEST_ASSERT_TRUE(macstatus::parseClaudeUsageFrame(line, frame));
  TEST_ASSERT_EQUAL_INT16(0, frame.fiveHourTenths);
  TEST_ASSERT_EQUAL_INT16(1000, frame.weekTenths);
  const char *bad[] = {"MSA1,,100,0", "MSA1,1,2,0,0", "MSA1,-2,2,0", "MSA1,1001,2,0",
                      "MSA1,1,2,2", "MSA1,+1,2,0", "MSA1, 1,2,0", "MSA1,1,2,", "MSA1,1,2,0,"};
  for (const char *payload : bad) {
    buildFrame(payload, line, sizeof(line));
    TEST_ASSERT_FALSE(macstatus::parseClaudeUsageFrame(line, frame));
    TEST_ASSERT_EQUAL_INT16(1000, frame.weekTenths);
  }
  TEST_ASSERT_FALSE(macstatus::parseClaudeUsageFrame("$MSA1,1,2,0*FFFF", frame));
  buildFrame("MSA1,-1,-1,1", line, sizeof(line));
  TEST_ASSERT_TRUE(macstatus::parseClaudeUsageFrame(line, frame));
  TEST_ASSERT_EQUAL_INT16(-1, frame.fiveHourTenths);
  TEST_ASSERT_TRUE(frame.stale);
}

void test_codex_aux_frame_shares_validation_but_not_tag() {
  macstatus::QuotaFrame frame;
  char line[80];
  buildFrame("MSA2,580,170,0", line, sizeof(line));
  TEST_ASSERT_TRUE(macstatus::parseCodexUsageFrame(line, frame));
  TEST_ASSERT_EQUAL_INT16(580, frame.fiveHourTenths);
  TEST_ASSERT_EQUAL_INT16(170, frame.weekTenths);
  TEST_ASSERT_FALSE(frame.stale);
  // Each tag belongs to one provider.
  TEST_ASSERT_FALSE(macstatus::parseClaudeUsageFrame(line, frame));
  buildFrame("MSA1,580,170,0", line, sizeof(line));
  TEST_ASSERT_FALSE(macstatus::parseCodexUsageFrame(line, frame));
  const char *bad[] = {"MSA2,1001,2,0", "MSA2,1,2", "MSA2,1,2,0,0", "MSA2,-2,2,0", "MSA20,1,2,0"};
  for (const char *payload : bad) {
    buildFrame(payload, line, sizeof(line));
    TEST_ASSERT_FALSE(macstatus::parseCodexUsageFrame(line, frame));
  }
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

void test_frame_envelope_is_shared_by_every_frame_type() {
  char line[40];
  buildFrame("MSQ1", line, sizeof(line));
  TEST_ASSERT_EQUAL_PTR(line + 5, macstatus::verifiedFrameEnd(line));
  TEST_ASSERT_TRUE(macstatus::validAuxFrame(line, "$MSQ1*"));
  TEST_ASSERT_FALSE(macstatus::validAuxFrame(line, "$MSQ2*"));
  line[1] = 'N';  // Payload changed after the checksum was computed.
  TEST_ASSERT_NULL(macstatus::verifiedFrameEnd(line));
  const char *malformed[] = {"", "MSQ1*0000", "$MSQ1", "$MSQ1*123", "$MSQ1*12345", "$MSQ1*12G4"};
  for (const char *candidate : malformed) TEST_ASSERT_NULL(macstatus::verifiedFrameEnd(candidate));
  TEST_ASSERT_NULL(macstatus::verifiedFrameEnd(nullptr));
}

void test_civil_date_from_local_epoch_days() {
  macstatus::CivilDate date = macstatus::civilFromDays(0);
  TEST_ASSERT_EQUAL_UINT16(1970, date.year);
  TEST_ASSERT_EQUAL_UINT8(1, date.month);
  TEST_ASSERT_EQUAL_UINT8(1, date.day);
  TEST_ASSERT_EQUAL_UINT8(4, date.weekday);  // Thursday
  date = macstatus::civilFromDays(1790800747UL / 86400UL);
  TEST_ASSERT_EQUAL_UINT16(2026, date.year);
  TEST_ASSERT_EQUAL_UINT8(9, date.month);
  TEST_ASSERT_EQUAL_UINT8(30, date.day);
  TEST_ASSERT_EQUAL_UINT8(3, date.weekday);  // Wednesday
  date = macstatus::civilFromDays(1709164800UL / 86400UL);
  TEST_ASSERT_EQUAL_UINT8(2, date.month);
  TEST_ASSERT_EQUAL_UINT8(29, date.day);  // 2024 leap day
  date = macstatus::civilFromDays(4102444799UL / 86400UL);
  TEST_ASSERT_EQUAL_UINT16(2099, date.year);
  TEST_ASSERT_EQUAL_UINT8(12, date.month);
  TEST_ASSERT_EQUAL_UINT8(31, date.day);
}

void test_line_reader_discards_overlong_and_empty_lines() {
  minidisplay::LineReader<8> reader;
  const char *input = "\n$ABC\r\n0123456789\n$OK\n";
  const char *lines[4] = {};
  size_t count = 0;
  for (const char *cursor = input; *cursor != '\0'; ++cursor) {
    const char *line = reader.push(*cursor);
    if (line != nullptr) {
      TEST_ASSERT_TRUE(count < 2);
      lines[count++] = line;
      if (count == 1) TEST_ASSERT_EQUAL_STRING("$ABC", line);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(2, count);
  TEST_ASSERT_EQUAL_STRING("$OK", lines[1]);
}

void test_status_formatting() {
  char text[16];
  minidisplay::formatPercent(-1, text, sizeof(text));
  TEST_ASSERT_EQUAL_STRING("--", text);
  minidisplay::formatPercent(725, text, sizeof(text));
  TEST_ASSERT_EQUAL_STRING("73%", text);
  minidisplay::formatRate(1023, text, sizeof(text));
  TEST_ASSERT_EQUAL_STRING("1023B/s", text);
  minidisplay::formatRate(347000, text, sizeof(text));
  TEST_ASSERT_EQUAL_STRING("338.9K/s", text);
  minidisplay::formatRate(UINT32_MAX, text, sizeof(text));
  TEST_ASSERT_EQUAL_STRING("4.0G/s", text);
  char country[3];
  char detail[4];
  minidisplay::splitNetworkLocation("US-WWWW", country, detail);
  TEST_ASSERT_EQUAL_STRING("US", country);
  TEST_ASSERT_EQUAL_STRING("WWW", detail);
  minidisplay::splitNetworkLocation("SG", country, detail);
  TEST_ASSERT_EQUAL_STRING("SG", detail);
  minidisplay::splitNetworkLocation("--", country, detail);
  TEST_ASSERT_EQUAL_STRING("--", country);
  TEST_ASSERT_EQUAL_STRING("--", detail);
}

}  // namespace

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_frame_envelope_is_shared_by_every_frame_type);
  RUN_TEST(test_civil_date_from_local_epoch_days);
  RUN_TEST(test_line_reader_discards_overlong_and_empty_lines);
  RUN_TEST(test_status_formatting);
  RUN_TEST(test_crc_standard_vector);
  RUN_TEST(test_claude_aux_frame_validation);
  RUN_TEST(test_codex_aux_frame_shares_validation_but_not_tag);
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
