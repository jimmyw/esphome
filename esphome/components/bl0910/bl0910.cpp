#include "bl0910.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "constant.h"
#include <cinttypes>

namespace esphome {
namespace bl0910 {

static const char *const TAG = "bl0910";

static uint8_t checksum_calc(uint8_t *data) {
  uint8_t checksum = 0;
  for (int i = 0; i < 5; i++) {
    checksum += data[i];
  }
  checksum &= 0xFF;
  checksum ^= 0xFF;

  return checksum;
}

void BL0910::write_register(uint8_t addr, uint8_t data_h, uint8_t data_m, uint8_t data_l) {
  uint8_t packet[6] = {0};
  packet[0] = BL0910_WRITE_COMMAND;
  packet[1] = addr;
  packet[2] = data_h;
  packet[3] = data_m;
  packet[4] = data_l;
  packet[5] = checksum_calc(packet);
  this->enable();
  this->transfer_array(packet, sizeof(packet) / sizeof(packet[0]));
  this->disable();
}

int32_t BL0910::read_register(uint8_t addr) {
  int32_t data = 0;
  uint8_t packet[6] = {0};

  packet[0] = BL0910_READ_COMMAND;
  packet[1] = addr;
  this->enable();
  this->transfer_array(packet, sizeof(packet) / sizeof(packet[0]));
  this->disable();

  // Verify checksum
  uint8_t checksum = BL0910_READ_COMMAND + addr;
  for (int i = 2; i < 5; i++) {
    checksum += packet[i];
  }
  checksum &= 0xFF;
  checksum ^= 0xFF;
  if (checksum != packet[5])  // checksum is byte 6
  {
    ESP_LOGE(TAG, "Checksum error calculated: %x != read: %x", checksum, packet[5]);
    return 0xFF000000;
  }

  return encode_uint24(packet[2], packet[3], packet[4]);
}

float BL0910::get_voltage(uint8_t channel) {
  return ((float) read_register(BL0910_REG_RMS[channel])) / this->voltage_reference_[channel];
}

float BL0910::get_current(uint8_t channel) {
  return ((float) read_register(BL0910_REG_RMS[channel])) / this->current_reference_[channel];
}

float BL0910::get_power(uint8_t channel) {
  return ((float) read_register(BL0910_REG_WATT[channel])) / this->power_reference_[channel];
}

float BL0910::get_energy(uint8_t channel) {
  return ((float) read_register(BL0910_REG_CF_CNT[channel])) / this->energy_reference_[channel];
}

float BL0910::get_frequency(void) {
  const float freq = (float) read_register(BL0910_REG_PERIOD);
  return 10000000.0 / freq;
}

float BL0910::get_temperature(void) {
  const float temp = (float) read_register(BL0910_REG_TPS1);
  return (temp - 64.0) * 12.5 / 59.0 - 40.0;
}

float BL0910::get_powerfactor(uint8_t channel, float freq) {
  const float angle = (float) read_register(BL0910_REG_ANGLE[channel]);
  return (360.0f * angle * freq) / 500000.0f;
}

void BL0910::update() { this->current_channel_ = 0; }

void BL0910::loop() {
  if (this->current_channel_ < NUM_CHANNELS) {
    if (voltage_sensor_[this->current_channel_])
      voltage_sensor_[this->current_channel_]->publish_state(get_voltage(this->current_channel_));
    if (current_sensor_[this->current_channel_])
      current_sensor_[this->current_channel_]->publish_state(get_current(this->current_channel_));
    if (power_sensor_[this->current_channel_])
      power_sensor_[this->current_channel_]->publish_state(get_power(this->current_channel_));
    if (energy_sensor_[this->current_channel_])
      energy_sensor_[this->current_channel_]->publish_state(get_energy(this->current_channel_));
    if (power_factor_sensor_[this->current_channel_])
      power_factor_sensor_[this->current_channel_]->publish_state(get_powerfactor(this->current_channel_, this->freq_));
    this->current_channel_++;
  } else if (this->current_channel_ == NUM_CHANNELS) {
    this->freq_ = get_frequency();
    if (frequency_sensor_)
      frequency_sensor_->publish_state(this->freq_);
    if (temperature_sensor_)
      temperature_sensor_->publish_state(get_temperature());
    this->current_channel_++;
  }
}

void BL0910::setup() {
  this->spi_setup();
  this->write_register(BL0910_REG_SOFT_RESET, 0x5A, 0x5A, 0x5A);  // Reset to default
  this->write_register(BL0910_REG_USR_WRPROT, 0x00, 0x55, 0x55);  // Enable User Operation Write
  this->write_register(BL0910_REG_MODE, 0x00, 0x00, 0x00);
  this->write_register(BL0910_REG_TPS_CTRL, 0x00, 0x07, 0xFF);
  this->write_register(BL0910_REG_FAST_RMS_CTRL, 0x40, 0xFF, 0xFF);
  this->write_register(BL0910_REG_GAIN1, 0x00, 0x00, 0x00);
}

void BL0910::dump_config() {  // NOLINT(readability-function-cognitive-complexity)
  ESP_LOGCONFIG(TAG, "BL0910:");
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (voltage_sensor_[i]) {
      char buf[20];
      snprintf(buf, sizeof(buf), "Voltage %d", i + 1);
      LOG_SENSOR("", buf, voltage_sensor_[i]);
    }

    if (current_sensor_[i]) {
      char buf[20];
      snprintf(buf, sizeof(buf), "Current %d", i + 1);
      LOG_SENSOR("", buf, current_sensor_[i]);
    }
    if (power_sensor_[i]) {
      char buf[20];
      snprintf(buf, sizeof(buf), "Power %d", i + 1);
      LOG_SENSOR("", buf, power_sensor_[i]);
    }
    if (energy_sensor_[i]) {
      char buf[20];
      snprintf(buf, sizeof(buf), "Energy %d", i + 1);
      LOG_SENSOR("", buf, energy_sensor_[i]);
    }
    if (power_factor_sensor_[i]) {
      char buf[20];
      snprintf(buf, sizeof(buf), "Power Factor %d", i + 1);
      LOG_SENSOR("", buf, power_factor_sensor_[i]);
    }
  }
  if (this->frequency_sensor_) {
    LOG_SENSOR("", "Frequency", this->frequency_sensor_);
  }
  if (this->temperature_sensor_) {
    LOG_SENSOR("", "Temperature", this->temperature_sensor_);
  }
}

}  // namespace bl0910
}  // namespace esphome
