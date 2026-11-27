#include "i2c_vote.h"

#include <cstring>
#include "esphome/core/log.h"

namespace esphome::i2c_vote {

ESPHOME_LOG_TAG(TAG, "i2c_vote");

i2c::ErrorCode I2CVoteBus::write_readv(uint8_t address, const uint8_t *write_buffer, size_t write_count,
                                       uint8_t *read_buffer, size_t read_count) {
  // Only a register read (address write, then read) can be repeated without side effects
  if (write_count == 0 || read_count == 0 || read_count > MAX_VOTE_READ)
    return this->parent_->write_readv(address, write_buffer, write_count, read_buffer, read_count);

  auto err = this->parent_->write_readv(address, write_buffer, write_count, read_buffer, read_count);
  if (err != i2c::ERROR_OK)
    return err;

  uint8_t second[MAX_VOTE_READ];
  err = this->parent_->write_readv(address, write_buffer, write_count, second, read_count);
  if (err != i2c::ERROR_OK)
    return err;
  if (std::memcmp(read_buffer, second, read_count) == 0)
    return i2c::ERROR_OK;

  uint8_t third[MAX_VOTE_READ];
  err = this->parent_->write_readv(address, write_buffer, write_count, third, read_count);
  if (err != i2c::ERROR_OK)
    return err;
  if (std::memcmp(read_buffer, third, read_count) == 0) {
    this->corrected_count_++;
    ESP_LOGV(TAG, "0x%02X: second copy differed, first and third agree", address);
    return i2c::ERROR_OK;
  }
  // Second and third agreeing is not trusted: after a glitch a device can return the same wrong
  // bytes on several reads in a row (seen as 0x0000 twice after a corrupted first copy).
  this->rejected_count_++;
  ESP_LOGW(TAG, "0x%02X: first and third read differ, read rejected", address);
  return i2c::ERROR_CRC;
}

void I2CVoteBus::dump_config() {
  ESP_LOGCONFIG(TAG,
                "I2C vote bus:\n"
                "  Register reads up to %u bytes are voted 2 out of 3",
                static_cast<unsigned>(MAX_VOTE_READ));
#ifdef USE_SENSOR
  LOG_SENSOR("  ", "Corrected", this->corrected_sensor_);
  LOG_SENSOR("  ", "Rejected", this->rejected_sensor_);
#endif
  LOG_UPDATE_INTERVAL(this);
}

void I2CVoteBus::update() {
#ifdef USE_SENSOR
  PUBLISH_STATE(this->corrected_sensor_, this->corrected_count_);
  PUBLISH_STATE(this->rejected_sensor_, this->rejected_count_);
#endif
}

}  // namespace esphome::i2c_vote
