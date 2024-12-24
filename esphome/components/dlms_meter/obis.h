#pragma once

namespace esphome {
namespace dlms_meter {

/*
 * Data types as per specification
 */

enum DataType {
  NULL_DATA = 0x00,
  BOOLEAN = 0x03,
  BIT_STRING = 0x04,
  DOUBLE_LONG = 0x05,
  DOUBLE_LONG_UNSIGNED = 0x06,
  OCTET_STRING = 0x09,
  VISIBLE_STRING = 0x0A,
  UTF8_STRING = 0x0C,
  BINARY_CODED_DECIMAL = 0x0D,
  INTEGER = 0x0F,
  LONG = 0x10,
  UNSIGNED = 0x11,
  LONG_UNSIGNED = 0x12,
  LONG64 = 0x14,
  LONG64_UNSIGNED = 0x15,
  ENUM = 0x16,
  FLOAT32 = 0x17,
  FLOAT64 = 0x18,
  DATE_TIME = 0x19,
  DATE = 0x1A,
  TIME = 0x1B,

  ARRAY = 0x01,
  STRUCTURE = 0x02,
  COMPACT_ARRAY = 0x13
};

enum Medium { ABSTRACT = 0x00, ELECTRICITY = 0x01, HEAT = 0x06, GAS = 0x07, WATER = 0x08 };

enum CodeType {
  UNKNOWN,
  TIMESTAMP,
  SERIAL_NUMBER,
  DEVICE_NAME,
  VOLTAGE_L1,
  VOLTAGE_L2,
  VOLTAGE_L3,
  CURRENT_L1,
  CURRENT_L2,
  CURRENT_L3,
  ACTIVE_POWER_PLUS,
  ACTIVE_POWER_MINUS,
  ACTIVE_ENERGY_PLUS,
  ACTIVE_ENERGY_MINUS,
  REACTIVE_ENERGY_PLUS,
  REACTIVE_ENERGY_MINUS,
  METER_NUMBER,  // EVN Special
  POWER_FACTOR   // EVN Special
};

enum Accuracy { SINGLE_DIGIT = 0xFF, DOUBLE_DIGIT = 0xFE };

/*
 * Data structure
 */

static const int DECODER_START_OFFSET =
    20;  // Offset for start of OBIS decoding, skip header, timestamp and break block

static const int OBIS_TYPE_OFFSET = 0;
static const int OBIS_LENGTH_OFFSET = 1;

static const int OBIS_CODE_OFFSET = 2;

static const int OBIS_A = 0;
static const int OBIS_B = 1;
static const int OBIS_C = 2;
static const int OBIS_D = 3;
static const int OBIS_E = 4;
static const int OBIS_F = 5;

/*
 * Metadata
 */

static const uint8_t ESPDM_TIMESTAMP[]{0x01, 0x00};

static const uint8_t ESPDM_SERIAL_NUMBER[]{0x60, 0x01};

static const uint8_t ESPDM_DEVICE_NAME[]{0x2A, 0x00};

/*
 * Voltage
 */

static const uint8_t ESPDM_VOLTAGE_L1[]{0x20, 0x07};

static const uint8_t ESPDM_VOLTAGE_L2[]{0x34, 0x07};

static const uint8_t ESPDM_VOLTAGE_L3[]{0x48, 0x07};

/*
 * Current
 */

static const uint8_t ESPDM_CURRENT_L1[]{0x1F, 0x07};

static const uint8_t ESPDM_CURRENT_L2[]{0x33, 0x07};

static const uint8_t ESPDM_CURRENT_L3[]{0x47, 0x07};

/*
 * Power
 */

static const uint8_t ESPDM_ACTIVE_POWER_PLUS[]{0x01, 0x07};

static const uint8_t ESPDM_ACTIVE_POWER_MINUS[]{0x02, 0x07};

/*
 * Active energy
 */

static const uint8_t ESPDM_ACTIVE_ENERGY_PLUS[]{0x01, 0x08};
static const uint8_t ESPDM_ACTIVE_ENERGY_MINUS[]{0x02, 0x08};

/*
 * Reactive energy
 */

static const uint8_t ESPDM_REACTIVE_ENERGY_PLUS[]{0x03, 0x08};
static const uint8_t ESPDM_REACTIVE_ENERGY_MINUS[]{0x04, 0x08};

/*
 * EVN Special
 */

static const uint8_t ESPDM_POWER_FACTOR[]{0x0D, 0x07};

}  // namespace dlms_meter
}  // namespace esphome
