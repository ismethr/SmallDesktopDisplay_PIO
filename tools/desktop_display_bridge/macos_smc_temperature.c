// Read-only AppleSMC temperature helper for SmallDesktopDisplayBridge.
//
// The SMC structures and read sequence follow the MIT-licensed Stats project:
// https://github.com/exelban/stats/tree/master/SMC
// Copyright (c) 2019 Serhiy Mytrovtsiy

#include <IOKit/IOKitLib.h>
#include <mach/mach.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  kSmcKernelIndex = 2,
  kSmcReadBytes = 5,
  kSmcReadIndex = 8,
  kSmcReadKeyInfo = 9,
};

typedef struct {
  uint8_t major;
  uint8_t minor;
  uint8_t build;
  uint8_t reserved;
  uint16_t release;
} SmcVersion;

typedef struct {
  uint16_t version;
  uint16_t length;
  uint32_t cpu_limit;
  uint32_t gpu_limit;
  uint32_t memory_limit;
} SmcPowerLimit;

typedef struct {
  IOByteCount32 data_size;
  uint32_t data_type;
  uint8_t attributes;
} SmcKeyInfo;

typedef struct {
  uint32_t key;
  SmcVersion version;
  SmcPowerLimit power_limit;
  SmcKeyInfo key_info;
  uint8_t result;
  uint8_t status;
  uint8_t data8;
  uint32_t data32;
  uint8_t bytes[32];
} SmcKeyData;

typedef struct {
  uint32_t data_size;
  char data_type[5];
  uint8_t bytes[32];
} SmcValue;

static uint32_t key_code(const char key[4]) {
  return (uint32_t)(uint8_t)key[0] << 24 |
         (uint32_t)(uint8_t)key[1] << 16 |
         (uint32_t)(uint8_t)key[2] << 8 |
         (uint32_t)(uint8_t)key[3];
}

static void key_string(uint32_t code, char output[5]) {
  output[0] = (char)((code >> 24) & 0xff);
  output[1] = (char)((code >> 16) & 0xff);
  output[2] = (char)((code >> 8) & 0xff);
  output[3] = (char)(code & 0xff);
  output[4] = '\0';
}

static kern_return_t smc_call(io_connect_t connection, SmcKeyData *input,
                              SmcKeyData *output) {
  size_t output_size = sizeof(*output);
  return IOConnectCallStructMethod(connection, kSmcKernelIndex, input,
                                   sizeof(*input), output, &output_size);
}

static int smc_read(io_connect_t connection, const char key[4], SmcValue *value) {
  SmcKeyData input = {0};
  SmcKeyData output = {0};
  input.key = key_code(key);
  input.data8 = kSmcReadKeyInfo;
  if (smc_call(connection, &input, &output) != KERN_SUCCESS || output.result != 0 ||
      output.key_info.data_size == 0 || output.key_info.data_size > sizeof(output.bytes)) {
    return 0;
  }

  value->data_size = output.key_info.data_size;
  key_string(output.key_info.data_type, value->data_type);
  input.key_info.data_size = output.key_info.data_size;
  input.data8 = kSmcReadBytes;
  memset(&output, 0, sizeof(output));
  if (smc_call(connection, &input, &output) != KERN_SUCCESS || output.result != 0) {
    return 0;
  }
  memcpy(value->bytes, output.bytes, value->data_size);
  return 1;
}

static int smc_value(io_connect_t connection, const char key[4], double *value) {
  SmcValue raw = {0};
  if (!smc_read(connection, key, &raw)) return 0;

  const uint16_t unsigned16 = (uint16_t)raw.bytes[0] << 8 | raw.bytes[1];
  const int16_t signed16 = (int16_t)unsigned16;
  if (strcmp(raw.data_type, "ui8 ") == 0) {
    *value = raw.bytes[0];
  } else if (strcmp(raw.data_type, "ui16") == 0) {
    *value = unsigned16;
  } else if (strcmp(raw.data_type, "ui32") == 0) {
    *value = (uint32_t)raw.bytes[0] << 24 | (uint32_t)raw.bytes[1] << 16 |
             (uint32_t)raw.bytes[2] << 8 | raw.bytes[3];
  } else if (strcmp(raw.data_type, "sp1e") == 0) {
    *value = signed16 / 16384.0;
  } else if (strcmp(raw.data_type, "sp3c") == 0) {
    *value = signed16 / 4096.0;
  } else if (strcmp(raw.data_type, "sp4b") == 0) {
    *value = signed16 / 2048.0;
  } else if (strcmp(raw.data_type, "sp5a") == 0) {
    *value = signed16 / 1024.0;
  } else if (strcmp(raw.data_type, "sp69") == 0) {
    *value = signed16 / 512.0;
  } else if (strcmp(raw.data_type, "sp78") == 0) {
    *value = signed16 / 256.0;
  } else if (strcmp(raw.data_type, "sp87") == 0) {
    *value = signed16 / 128.0;
  } else if (strcmp(raw.data_type, "sp96") == 0) {
    *value = signed16 / 64.0;
  } else if (strcmp(raw.data_type, "spa5") == 0) {
    *value = signed16 / 32.0;
  } else if (strcmp(raw.data_type, "spb4") == 0) {
    *value = signed16 / 16.0;
  } else if (strcmp(raw.data_type, "spf0") == 0) {
    *value = signed16;
  } else if (strcmp(raw.data_type, "flt ") == 0 && raw.data_size >= sizeof(float)) {
    float float_value = 0;
    memcpy(&float_value, raw.bytes, sizeof(float_value));
    *value = float_value;
  } else {
    return 0;
  }
  return isfinite(*value);
}

int main(void) {
  io_service_t service = IOServiceGetMatchingService(
      MACH_PORT_NULL, IOServiceMatching("AppleSMC"));
  if (service == IO_OBJECT_NULL) return 1;

  io_connect_t connection = IO_OBJECT_NULL;
  const kern_return_t open_result =
      IOServiceOpen(service, mach_task_self(), 0, &connection);
  IOObjectRelease(service);
  if (open_result != KERN_SUCCESS) return 1;

  double key_count_value = 0;
  if (!smc_value(connection, "#KEY", &key_count_value) || key_count_value <= 0 ||
      key_count_value >= 10000) {
    IOServiceClose(connection);
    return 1;
  }

  const uint32_t key_count = (uint32_t)key_count_value;
  for (uint32_t index = 0; index < key_count; ++index) {
    SmcKeyData input = {0};
    SmcKeyData output = {0};
    input.data8 = kSmcReadIndex;
    input.data32 = index;
    if (smc_call(connection, &input, &output) != KERN_SUCCESS || output.result != 0) {
      continue;
    }
    char key[5];
    key_string(output.key, key);
    if (key[0] != 'T') continue;

    double temperature = 0;
    if (smc_value(connection, key, &temperature) && temperature >= 1 &&
        temperature <= 125) {
      printf("[%s] %.1f\n", key, temperature);
    }
  }

  IOServiceClose(connection);
  return 0;
}
