#include "dlms_meter.h"
#include "mbus.h"
#include "dlms.h"
#include "obis.h"
#include <vector>
#include "esphome/core/log.h"

#if defined(ESP8266)
#include <bearssl/bearssl.h>
#endif

#define PROVIDER_EVN

namespace esphome {
namespace dlms_meter {

// DlmsMeterComponent::DlmsMeterComponent(uart::UARTComponent *parent) : uart::UARTDevice(parent) {}

void DlmsMeterComponent::setup() {
  ESP_LOGI(TAG, "DLMS smart meter component v%s started", DLMS_METER_VERSION);

  // TODO
  // EVN sample key
  uint8_t key[] = {0x36, 0xC6, 0x66, 0x39, 0xE4, 0x8A, 0x8C, 0xA4, 0xD6, 0xBC, 0x8B, 0x28, 0x2A, 0x79, 0x3B, 0xBB};
  // uint8_t key[] = {0xBC, 0x02, 0xF5, 0x42, 0xE6, 0x71, 0x9E, 0xD6, 0xDF, 0xE8, 0x4C, 0x6F, 0x18, 0x0C, 0x7A, 0x68};
  this->set_key(key, 16);
}

void DlmsMeterComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "DLMS Meter dump_config");
  this->check_uart_settings(2400);
}

void DlmsMeterComponent::loop() {
  unsigned long currentTime = millis();

  while (available())  // Read while data is available
  {
    uint8_t c;
    this->read_byte(&c);
    this->receiveBuffer.push_back(c);

    this->lastRead = currentTime;
    // fix for ESPHOME 2022.12 -> added 10ms delay
    delay(10);
  }

  if (!this->receiveBuffer.empty() && currentTime - this->lastRead > this->readTimeout) {
    log_packet(this->receiveBuffer);

    // Verify and parse M-Bus frames

    ESP_LOGV(TAG, "Parsing M-Bus frames");

    uint16_t frameOffset = 0;          // Offset is used if the M-Bus message is split into multiple frames
    std::vector<uint8_t> mbusPayload;  // Contains the data of the payload

    while (true) {
      ESP_LOGV(TAG, "MBUS: Parsing frame");

      // Check start bytes
      if (this->receiveBuffer[frameOffset + MBUS_START1_OFFSET] != 0x68 ||
          this->receiveBuffer[frameOffset + MBUS_START2_OFFSET] != 0x68) {
        ESP_LOGE(TAG, "MBUS: Start bytes do not match");
        return abort();
      }

      // Both length bytes must be identical
      if (this->receiveBuffer[frameOffset + MBUS_LENGTH1_OFFSET] !=
          this->receiveBuffer[frameOffset + MBUS_LENGTH2_OFFSET]) {
        ESP_LOGE(TAG, "MBUS: Length bytes do not match");
        return abort();
      }

      uint8_t frameLength = this->receiveBuffer[frameOffset + MBUS_LENGTH1_OFFSET];  // Get length of this frame

      // Check if received data is enough for the given frame length
      if (this->receiveBuffer.size() - frameOffset < frameLength + 3) {
        ESP_LOGE(TAG, "MBUS: Frame too big for received data");
        return abort();
      }

      if (this->receiveBuffer[frameOffset + frameLength + MBUS_HEADER_INTRO_LENGTH + MBUS_FOOTER_LENGTH - 1] != 0x16) {
        ESP_LOGE(TAG, "MBUS: Invalid stop byte");
        return abort();
      }

      mbusPayload.insert(mbusPayload.end(), &this->receiveBuffer[frameOffset + MBUS_FULL_HEADER_LENGTH],
                         &this->receiveBuffer[frameOffset + MBUS_HEADER_INTRO_LENGTH + frameLength]);

      frameOffset += MBUS_HEADER_INTRO_LENGTH + frameLength + MBUS_FOOTER_LENGTH;

      if (frameOffset >= this->receiveBuffer.size())  // No more data to read, exit loop
      {
        break;
      }
    }

    // Verify and parse DLMS header

    ESP_LOGV(TAG, "Parsing DLMS header");

    if (mbusPayload.size() < 20)  // If the payload is too short we need to abort
    {
      ESP_LOGE(TAG, "DLMS: Payload too short");
      return abort();
    }

    if (mbusPayload[DLMS_CIPHER_OFFSET] != 0xDB)  // Only general-glo-ciphering is supported (0xDB)
    {
      ESP_LOGE(TAG, "DLMS: Unsupported cipher");
      return abort();
    }

    uint8_t systitleLength = mbusPayload[DLMS_SYST_OFFSET];

    if (systitleLength != 0x08)  // Only system titles with length of 8 are supported
    {
      ESP_LOGE(TAG, "DLMS: Unsupported system title length");
      return abort();
    }

    uint16_t messageLength = mbusPayload[DLMS_LENGTH_OFFSET];
    int headerOffset = 0;

#if defined(PROVIDER_EVN)
    // for some reason EVN seems to set the standard "length" field to 0x81 and then the actual length is in the next
    // byte
    if (messageLength == 0x81 && mbusPayload[DLMS_LENGTH_OFFSET + 1] == 0xF8 &&
        mbusPayload[DLMS_LENGTH_OFFSET + 2] == 0x20) {
      messageLength = mbusPayload[DLMS_LENGTH_OFFSET + 1];
      headerOffset = 1;
    } else {
      ESP_LOGE(TAG, "Wrong Length - Security Control Byte sequence detected for provider EVN");
    }
#else
    if (messageLength == 0x82) {
      ESP_LOGV(TAG, "DLMS: Message length > 127");

      memcpy(&messageLength, &mbusPayload[DLMS_LENGTH_OFFSET + 1], 2);
      messageLength = swap_uint16(messageLength);

      headerOffset = DLMS_HEADER_EXT_OFFSET;  // Header is now 2 bytes longer due to length > 127
    } else {
      ESP_LOGV(TAG, "DLMS: Message length <= 127");
    }
#endif

    messageLength -= DLMS_LENGTH_CORRECTION;  // Correct message length due to part of header being included in length

    if (mbusPayload.size() - DLMS_HEADER_LENGTH - headerOffset != messageLength) {
      ESP_LOGV(TAG, "lengths: %d, %d, %d, %d", mbusPayload.size(), DLMS_HEADER_LENGTH, headerOffset, messageLength);
      ESP_LOGE(TAG, "DLMS: Message has invalid length");
      return abort();
    }

    if (mbusPayload[headerOffset + DLMS_SECBYTE_OFFSET] != 0x21 &&
        mbusPayload[headerOffset + DLMS_SECBYTE_OFFSET] !=
            0x20)  // Only certain security suite is supported (0x21 || 0x20)
    {
      ESP_LOGE(TAG, "DLMS: Unsupported security control byte");
      return abort();
    }

    // Decryption

    ESP_LOGV(TAG, "Decrypting payload");

    uint8_t iv[12];  // Reserve space for the IV, always 12 bytes
    // Copy system title to IV (System title is before length; no header offset needed!)
    // Add 1 to the offset in order to skip the system title length byte
    memcpy(&iv[0], &mbusPayload[DLMS_SYST_OFFSET + 1], systitleLength);
    memcpy(&iv[8], &mbusPayload[headerOffset + DLMS_FRAMECOUNTER_OFFSET],
           DLMS_FRAMECOUNTER_LENGTH);  // Copy frame counter to IV

    uint8_t plaintext[messageLength];

#if defined(ESP8266)
    memcpy(plaintext, &mbusPayload[headerOffset + DLMS_PAYLOAD_OFFSET], messageLength);
    br_gcm_context gcmCtx;
    br_aes_ct_ctr_keys bc;
    br_aes_ct_ctr_init(&bc, this->key, this->keyLength);
    br_gcm_init(&gcmCtx, &bc.vtable, br_ghash_ctmul32);
    br_gcm_reset(&gcmCtx, iv, sizeof(iv));
    br_gcm_flip(&gcmCtx);
    br_gcm_run(&gcmCtx, 0, plaintext, messageLength);
#elif defined(ESP32)
    mbedtls_gcm_init(&this->aes);
    mbedtls_gcm_setkey(&this->aes, MBEDTLS_CIPHER_ID_AES, this->key, this->keyLength * 8);

    mbedtls_gcm_auth_decrypt(&this->aes, messageLength, iv, sizeof(iv), NULL, 0, NULL, 0,
                             &mbusPayload[headerOffset + DLMS_PAYLOAD_OFFSET], plaintext);

    mbedtls_gcm_free(&this->aes);
#else
#error "Invalid Platform"
#endif

    ESP_LOGV(TAG, "Decrypted payload: %d", messageLength);
    ESP_LOGV(TAG, format_hex_pretty(&plaintext[0], messageLength).c_str());

    if (plaintext[0] != 0x0F || plaintext[5] != 0x0C) {
      ESP_LOGE(TAG, "OBIS: Packet was decrypted but data is invalid");
      return abort();
    }

    // Decoding

    ESP_LOGV(TAG, "Decoding payload");

    MeterData data;
    int currentPosition = DECODER_START_OFFSET;

    do {
      if (plaintext[currentPosition + OBIS_TYPE_OFFSET] != DataType::OctetString) {
        ESP_LOGE(TAG, "OBIS: Unsupported OBIS header type: %x", plaintext[currentPosition + OBIS_TYPE_OFFSET]);
        return abort();
      }

      uint8_t obisCodeLength = plaintext[currentPosition + OBIS_LENGTH_OFFSET];

      if (obisCodeLength != 0x06 && obisCodeLength != 0x0C) {
        ESP_LOGE(TAG, "OBIS: Unsupported OBIS header length: %x", obisCodeLength);
        return abort();
      }

      uint8_t obisCode[obisCodeLength];
      memcpy(&obisCode[0], &plaintext[currentPosition + OBIS_CODE_OFFSET], obisCodeLength);  // Copy OBIS code to array

#if defined(PROVIDER_EVN)
      bool timestampFound = false;
      bool meterNumberFound = false;
      // Do not advance Position when reading the Timestamp at DECODER_START_OFFSET
      if ((obisCodeLength == 0x0C) && (currentPosition == DECODER_START_OFFSET)) {
        timestampFound = true;
      } else if ((currentPosition != DECODER_START_OFFSET) && plaintext[currentPosition - 1] == 0xFF) {
        meterNumberFound = true;
      } else {
        currentPosition += obisCodeLength + 2;  // Advance past code and position
      }
#else
      currentPosition += obisCodeLength + 2;  // Advance past code, position and type
#endif

      uint8_t dataType = plaintext[currentPosition];
      currentPosition++;  // Advance past data type

      uint8_t dataLength = 0x00;

      CodeType codeType = CodeType::Unknown;

      ESP_LOGV(TAG, "obisCode (OBIS_A): %d", obisCode[OBIS_A]);
      ESP_LOGV(TAG, "currentPosition: %d", currentPosition);

      if (obisCode[OBIS_A] == Medium::Electricity) {
        // Compare C and D against code
        if (memcmp(&obisCode[OBIS_C], ESPDM_VOLTAGE_L1, 2) == 0) {
          codeType = CodeType::VoltageL1;
        } else if (memcmp(&obisCode[OBIS_C], ESPDM_VOLTAGE_L2, 2) == 0) {
          codeType = CodeType::VoltageL2;
        } else if (memcmp(&obisCode[OBIS_C], ESPDM_VOLTAGE_L3, 2) == 0) {
          codeType = CodeType::VoltageL3;
        }

        else if (memcmp(&obisCode[OBIS_C], ESPDM_CURRENT_L1, 2) == 0) {
          codeType = CodeType::CurrentL1;
        } else if (memcmp(&obisCode[OBIS_C], ESPDM_CURRENT_L2, 2) == 0) {
          codeType = CodeType::CurrentL2;
        } else if (memcmp(&obisCode[OBIS_C], ESPDM_CURRENT_L3, 2) == 0) {
          codeType = CodeType::CurrentL3;
        }

        else if (memcmp(&obisCode[OBIS_C], ESPDM_ACTIVE_POWER_PLUS, 2) == 0) {
          codeType = CodeType::ActivePowerPlus;
        } else if (memcmp(&obisCode[OBIS_C], ESPDM_ACTIVE_POWER_MINUS, 2) == 0) {
          codeType = CodeType::ActivePowerMinus;
        }
#if defined(PROVIDER_EVN)
        else if (memcmp(&obisCode[OBIS_C], ESPDM_POWER_FACTOR, 2) == 0) {
          codeType = CodeType::PowerFactor;
        }
#endif

        else if (memcmp(&obisCode[OBIS_C], ESPDM_ACTIVE_ENERGY_PLUS, 2) == 0) {
          codeType = CodeType::ActiveEnergyPlus;
        } else if (memcmp(&obisCode[OBIS_C], ESPDM_ACTIVE_ENERGY_MINUS, 2) == 0) {
          codeType = CodeType::ActiveEnergyMinus;
        }

        else if (memcmp(&obisCode[OBIS_C], ESPDM_REACTIVE_ENERGY_PLUS, 2) == 0) {
          codeType = CodeType::ReactiveEnergyPlus;
        } else if (memcmp(&obisCode[OBIS_C], ESPDM_REACTIVE_ENERGY_MINUS, 2) == 0) {
          codeType = CodeType::ReactiveEnergyMinus;
        } else {
          ESP_LOGW(TAG, "OBIS: Unsupported OBIS code OBIS_C: %d, OBIS_D: %d", obisCode[OBIS_C], obisCode[OBIS_D]);
        }
      } else if (obisCode[OBIS_A] == Medium::Abstract) {
        if (memcmp(&obisCode[OBIS_C], ESPDM_TIMESTAMP, 2) == 0) {
          codeType = CodeType::Timestamp;
        } else if (memcmp(&obisCode[OBIS_C], ESPDM_SERIAL_NUMBER, 2) == 0) {
          codeType = CodeType::SerialNumber;
        } else if (memcmp(&obisCode[OBIS_C], ESPDM_DEVICE_NAME, 2) == 0) {
          codeType = CodeType::DeviceName;
        }

        else {
          ESP_LOGW(TAG, "OBIS: Unsupported OBIS code OBIS_C: %d, OBIS_D: %d", obisCode[OBIS_C], obisCode[OBIS_D]);
        }
      }
#if defined(PROVIDER_EVN)
      // Needed so the Timestamp at DECODER_START_OFFSET gets read correctly
      // as it doesn't have an obisMedium
      else if (timestampFound == true) {
        ESP_LOGV(TAG, "Found Timestamp without obisMedium");
        codeType = CodeType::Timestamp;
      } else if (meterNumberFound == true) {
        ESP_LOGV(TAG, "Found MeterNumber without obisMedium");
        codeType = CodeType::MeterNumber;
      }
#endif

      else {
        ESP_LOGE(TAG, "OBIS: Unsupported OBIS medium: %x", obisCode[OBIS_A]);
        return abort();
      }

      uint8_t uint8Value;
      uint16_t uint16Value;
      uint32_t uint32Value;
      float floatValue;

      switch (dataType) {
        case DataType::DoubleLongUnsigned:
          dataLength = 4;

          memcpy(&uint32Value, &plaintext[currentPosition], 4);  // Copy bytes to integer
          uint32Value = swap_uint32(uint32Value);                // Swap bytes

          floatValue = uint32Value;  // Ignore decimal digits for now

          if (codeType == CodeType::ActivePowerPlus && this->active_power_plus != NULL &&
              this->active_power_plus->state != floatValue)
            this->active_power_plus->publish_state(floatValue);
          else if (codeType == CodeType::ActivePowerMinus && this->active_power_minus != NULL &&
                   this->active_power_minus->state != floatValue)
            this->active_power_minus->publish_state(floatValue);

          else if (codeType == CodeType::ActiveEnergyPlus && this->active_energy_plus != NULL &&
                   this->active_energy_plus->state != floatValue)
            this->active_energy_plus->publish_state(floatValue);
          else if (codeType == CodeType::ActiveEnergyMinus && this->active_energy_minus != NULL &&
                   this->active_energy_minus->state != floatValue)
            this->active_energy_minus->publish_state(floatValue);

          else if (codeType == CodeType::ReactiveEnergyPlus && this->reactive_energy_plus != NULL &&
                   this->reactive_energy_plus->state != floatValue)
            this->reactive_energy_plus->publish_state(floatValue);
          else if (codeType == CodeType::ReactiveEnergyMinus && this->reactive_energy_minus != NULL &&
                   this->reactive_energy_minus->state != floatValue)
            this->reactive_energy_minus->publish_state(floatValue);

          break;
        case DataType::LongUnsigned:
          dataLength = 2;

          memcpy(&uint16Value, &plaintext[currentPosition], 2);  // Copy bytes to integer
          uint16Value = swap_uint16(uint16Value);                // Swap bytes

          if (plaintext[currentPosition + 5] == Accuracy::SingleDigit)
            floatValue = uint16Value / 10.0;  // Divide by 10 to get decimal places
          else if (plaintext[currentPosition + 5] == Accuracy::DoubleDigit)
            floatValue = uint16Value / 100.0;  // Divide by 100 to get decimal places
          else
            floatValue = uint16Value;  // No decimal places

          // TODO!
          if (codeType == CodeType::VoltageL1) {
            ESP_LOGW(TAG, "TF %f", floatValue);
            this->wtf_sensor_->publish_state(floatValue);
            data.wtf = floatValue;
          }

          if (codeType == CodeType::CurrentL1) {
            ESP_LOGW(TAG, "TF2 %f", floatValue);
            // DLMS_METER_PUBLISH_SENSOR(wtf, floatValue);
            // this->wtf_sensor_->publish_state(floatValue);
            data.wtf2 = floatValue;
          }

          if (codeType == CodeType::VoltageL1 && this->voltage_l1 != NULL && this->voltage_l1->state != floatValue)
            this->voltage_l1->publish_state(floatValue);
          else if (codeType == CodeType::VoltageL2 && this->voltage_l2 != NULL && this->voltage_l2->state != floatValue)
            this->voltage_l2->publish_state(floatValue);
          else if (codeType == CodeType::VoltageL3 && this->voltage_l3 != NULL && this->voltage_l3->state != floatValue)
            this->voltage_l3->publish_state(floatValue);

          else if (codeType == CodeType::CurrentL1 && this->current_l1 != NULL && this->current_l1->state != floatValue)
            this->current_l1->publish_state(floatValue);
          else if (codeType == CodeType::CurrentL2 && this->current_l2 != NULL && this->current_l2->state != floatValue)
            this->current_l2->publish_state(floatValue);
          else if (codeType == CodeType::CurrentL3 && this->current_l3 != NULL && this->current_l3->state != floatValue)
            this->current_l3->publish_state(floatValue);

#if defined(PROVIDER_EVN)
          else if (codeType == CodeType::PowerFactor && this->power_factor != NULL &&
                   this->power_factor->state != floatValue)
            this->power_factor->publish_state(floatValue / 1000.0);
#endif

          break;
        case DataType::OctetString:
          ESP_LOGV(TAG, "Arrived on OctetString");
          ESP_LOGV(TAG, "currentPosition: %d, plaintext: %d", currentPosition, plaintext[currentPosition]);

          dataLength = plaintext[currentPosition];
          currentPosition++;  // Advance past string length

          if (codeType == CodeType::Timestamp && this->timestamp != NULL)  // Handle timestamp generation
          {
            char timestamp[21];  // 0000-00-00T00:00:00Z

            uint16_t year;
            uint8_t month;
            uint8_t day;

            uint8_t hour;
            uint8_t minute;
            uint8_t second;

            memcpy(&uint16Value, &plaintext[currentPosition], 2);
            year = swap_uint16(uint16Value);

            memcpy(&month, &plaintext[currentPosition + 2], 1);
            memcpy(&day, &plaintext[currentPosition + 3], 1);

            memcpy(&hour, &plaintext[currentPosition + 5], 1);
            memcpy(&minute, &plaintext[currentPosition + 6], 1);
            memcpy(&second, &plaintext[currentPosition + 7], 1);

            sprintf(timestamp, "%04u-%02u-%02uT%02u:%02u:%02uZ", year, month, day, hour, minute, second);

            this->timestamp->publish_state(timestamp);
          }
#if defined(PROVIDER_EVN)
          else if (codeType == CodeType::MeterNumber && this->meternumber != NULL) {
            ESP_LOGV(TAG, "Constructing MeterNumber...");
            char meterNumber[13];  // 121110284568

            memcpy(meterNumber, &plaintext[currentPosition], dataLength);
            meterNumber[12] = '\0';

            this->meternumber->publish_state(meterNumber);
          }
#endif

          break;
        default:
          ESP_LOGE(TAG, "OBIS: Unsupported OBIS data type: %x", dataType);
          return abort();
          break;
      }

      currentPosition += dataLength;  // Skip data length

#if defined(PROVIDER_EVN)
      // Don't skip the break on the first Timestamp, as there's none
      if (timestampFound == false) {
        currentPosition += 2;  // Skip break after data
      }
#else
      currentPosition += 2;  // Skip break after data
#endif

      if (plaintext[currentPosition] == 0x0F) {  // There is still additional data for this type, skip it
        // on EVN Meters the additional data (no additional Break) is only 3 Bytes + 1 Byte for the "there is data" Byte
#if defined(PROVIDER_EVN)
        currentPosition += 4;  // Skip additional data and additional break; this will jump out of bounds on last frame
#else
        currentPosition += 6;  // Skip additional data and additional break; this will jump out of bounds on last frame
#endif
      }
    } while (currentPosition <= messageLength);  // Loop until arrived at end

    this->receiveBuffer.clear();  // Reset buffer

    ESP_LOGI(TAG, "Received valid data");
    this->publish_sensors(data);

    /*if(this->mqtt_client != NULL)
    {
        this->mqtt_client->publish_json(this->topic, [=](JsonObject root)
        {
            if(this->voltage_l1 != NULL)
            {
                root["voltage_l1"] = this->voltage_l1->state;
                root["voltage_l2"] = this->voltage_l2->state;
                root["voltage_l3"] = this->voltage_l3->state;
            }

            if(this->current_l1 != NULL)
            {
                root["current_l1"] = this->current_l1->state;
                root["current_l2"] = this->current_l2->state;
                root["current_l3"] = this->current_l3->state;
            }

            if(this->active_power_plus != NULL)
            {
                root["active_power_plus"] = this->active_power_plus->state;
                root["active_power_minus"] = this->active_power_minus->state;
            }

            if(this->active_energy_plus != NULL)
            {
                root["active_energy_plus"] = this->active_energy_plus->state;
                root["active_energy_minus"] = this->active_energy_minus->state;
            }

            if(this->reactive_energy_plus != NULL)
            {
                root["reactive_energy_plus"] = this->reactive_energy_plus->state;
                root["reactive_energy_minus"] = this->reactive_energy_minus->state;
            }

            if(this->timestamp != NULL)
            {
                root["timestamp"] = this->timestamp->state;
            }
// EVN Special
            if(this->power_factor != NULL)
            {
                root["power_factor"] = this->power_factor->state;
            }
            if(this->meternumber != NULL)
            {
                root["meternumber"] = this->meternumber->state;
            }

        });
    }*/
  }
}

void DlmsMeterComponent::abort() { this->receiveBuffer.clear(); }

uint16_t DlmsMeterComponent::swap_uint16(uint16_t val) { return (val << 8) | (val >> 8); }

uint32_t DlmsMeterComponent::swap_uint32(uint32_t val) {
  val = ((val << 8) & 0xFF00FF00) | ((val >> 8) & 0xFF00FF);
  return (val << 16) | (val >> 16);
}

void DlmsMeterComponent::set_key(uint8_t key[], size_t keyLength) {
  memcpy(&this->key[0], &key[0], keyLength);
  this->keyLength = keyLength;
}

void DlmsMeterComponent::set_voltage_sensors(sensor::Sensor *voltage_l1, sensor::Sensor *voltage_l2,
                                             sensor::Sensor *voltage_l3) {
  this->voltage_l1 = voltage_l1;
  this->voltage_l2 = voltage_l2;
  this->voltage_l3 = voltage_l3;
}
void DlmsMeterComponent::set_current_sensors(sensor::Sensor *current_l1, sensor::Sensor *current_l2,
                                             sensor::Sensor *current_l3) {
  this->current_l1 = current_l1;
  this->current_l2 = current_l2;
  this->current_l3 = current_l3;
}

void DlmsMeterComponent::set_active_power_sensors(sensor::Sensor *active_power_plus,
                                                  sensor::Sensor *active_power_minus) {
  this->active_power_plus = active_power_plus;
  this->active_power_minus = active_power_minus;
}

void DlmsMeterComponent::set_active_energy_sensors(sensor::Sensor *active_energy_plus,
                                                   sensor::Sensor *active_energy_minus) {
  this->active_energy_plus = active_energy_plus;
  this->active_energy_minus = active_energy_minus;
}

void DlmsMeterComponent::set_reactive_energy_sensors(sensor::Sensor *reactive_energy_plus,
                                                     sensor::Sensor *reactive_energy_minus) {
  this->reactive_energy_plus = reactive_energy_plus;
  this->reactive_energy_minus = reactive_energy_minus;
}

void DlmsMeterComponent::set_timestamp_sensor(text_sensor::TextSensor *timestamp) { this->timestamp = timestamp; }

void DlmsMeterComponent::set_evnspecial_sensor(sensor::Sensor *power_factor, text_sensor::TextSensor *meternumber) {
  this->power_factor = power_factor;
  this->meternumber = meternumber;
}

// void DlmsMeterComponent::enable_mqtt(mqtt::MQTTClientComponent *mqtt_client, const char *topic)
// {
//     this->mqtt_client = mqtt_client;
//     this->topic = topic;
// }

void DlmsMeterComponent::log_packet(std::vector<uint8_t> data) { ESP_LOGV(TAG, format_hex_pretty(data).c_str()); }

}  // namespace dlms_meter
}  // namespace esphome