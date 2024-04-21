#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"

#include "mbus.h"
#include "dlms.h"
#include "obis.h"

#if defined(ESP32)
#include "mbedtls/gcm.h"
#endif

#if defined(ESP8266)
#include <bearssl/bearssl.h>
#endif

#include <vector>

#ifndef METER_PROVIDER
#define METER_PROVIDER ""
#endif

static const char *const DLMS_METER_VERSION = "0.9.1";
static const char *const TAG = "dlms_meter";

namespace esphome {
namespace dlms_meter {

// DLMS_METER_**_LIST generated by ESPHome and written in esphome/core/defines

#if !defined(DLMS_METER_SENSOR_LIST) && !defined(DLMS_METER_TEXT_SENSOR_LIST)
// Neither set, set it to a dummy value to not break build
#define DLMS_METER_TEXT_SENSOR_LIST(F, SEP)
#endif

#if defined(DLMS_METER_SENSOR_LIST) && defined(DLMS_METER_TEXT_SENSOR_LIST)
#define DLMS_METER_BOTH ,
#else
#define DLMS_METER_BOTH
#endif

#ifndef DLMS_METER_SENSOR_LIST
#define DLMS_METER_SENSOR_LIST(F, SEP)
#endif

#ifndef DLMS_METER_TEXT_SENSOR_LIST
#define DLMS_METER_TEXT_SENSOR_LIST(F, SEP)
#endif

#define DLMS_METER_DATA_SENSOR(s) s
#define DLMS_METER_COMMA ,

struct MeterData {
  float voltage_l1;             // Voltage L1
  float voltage_l2;             // Voltage L2
  float voltage_l3;             // Voltage L3
  float current_l1;             // Current L1
  float current_l2;             // Current L2
  float current_l3;             // Current L3
  float active_power_plus;      // Active power taken from grid
  float active_power_minus;     // Active power put into grid
  float active_energy_plus;     // Active energy taken from grid
  float active_energy_minus;    // Active energy put into grid
  float reactive_energy_plus;   // Reactive energy taken from grid
  float reactive_energy_minus;  // Reactive energy put into grid
  std::string timestamp;        // Text sensor for the timestamp value

  // EVN
  float power_factor;           // Power Factor
  std::string meternumber;      // Text sensor for the meterNumber value
};

class DlmsMeterComponent : public Component, public uart::UARTDevice {
 public:
  DlmsMeterComponent() = default;

  void setup() override;
  void dump_config() override;
  void loop() override;

  void set_decryption_key(const uint8_t *decryption_key, size_t decryption_key_length);

  void publish_sensors(MeterData &data) {
#define DLMS_METER_PUBLISH_SENSOR(s) \
  if (this->s##_sensor_ != nullptr) \
    s##_sensor_->publish_state(data.s);
    DLMS_METER_SENSOR_LIST(DLMS_METER_PUBLISH_SENSOR, )

#define DLMS_METER_PUBLISH_TEXT_SENSOR(s) \
  if (this->s##_text_sensor_ != nullptr) \
    s##_text_sensor_->publish_state(data.s);
    // s##_text_sensor_->publish_state(data.s.c_str());
    DLMS_METER_TEXT_SENSOR_LIST(DLMS_METER_PUBLISH_TEXT_SENSOR, )
  }

  DLMS_METER_SENSOR_LIST(SUB_SENSOR, )
  DLMS_METER_TEXT_SENSOR_LIST(SUB_TEXT_SENSOR, )

 protected:
  std::vector<uint8_t> receive_buffer_;  // Stores the packet currently being received
  unsigned long last_read_ = 0;          // Timestamp when data was last read
  int read_timeout_ = 1000;              // Time to wait after last byte before considering data complete

  uint8_t decryption_key_[16];   // Stores the decryption key
  size_t decryption_key_length_;  // Stores the decryption key length (usually 16 bytes)

#if defined(ESP32)
  mbedtls_gcm_context aes;  // AES context used for decryption
#endif

  uint16_t swap_uint16(uint16_t val);
  uint32_t swap_uint32(uint32_t val);
  void log_packet(std::vector<uint8_t> data);
  void abort();
};

}  // namespace dlms_meter
}  // namespace esphome
