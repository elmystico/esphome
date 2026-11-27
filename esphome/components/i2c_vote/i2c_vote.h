#pragma once

#include "esphome/components/i2c/i2c_bus.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

namespace esphome::i2c_vote {

/// Register reads longer than this are passed through without voting.
static constexpr size_t MAX_VOTE_READ = 32;

/// A virtual I2C bus that votes on register reads (2 out of 3).
///
/// I2C has no checksum: a glitch on SCL can make a device lose bit sync, and the controller
/// then reads wrong bytes and still acknowledges them. For a register read (address write
/// followed by a read), this bus reads twice and passes the data on when both copies match.
/// When they differ it reads a third time and passes the data on only when the third copy
/// matches the first; otherwise it returns ERROR_CRC instead of wrong data.
///
/// Only transfers with both a write and a read part are repeated. Plain reads and plain writes
/// pass through unchanged, because repeating them can consume data (FIFOs, measurement results)
/// or clear status flags.
class I2CVoteBus final : public PollingComponent, public i2c::I2CBus {
 public:
  void set_parent(i2c::I2CBus *parent) { this->parent_ = parent; }

  i2c::ErrorCode write_readv(uint8_t address, const uint8_t *write_buffer, size_t write_count, uint8_t *read_buffer,
                             size_t read_count) override;

#ifdef I2C_PORT_FREQUENCY_COUNT
  i2c::ErrorCode switch_frequency(uint32_t frequency) override { return this->parent_->switch_frequency(frequency); }
  uint32_t get_frequency() const override { return this->parent_->get_frequency(); }
#endif

  void dump_config() override;
  void update() override;

#ifdef USE_SENSOR
  SUB_SENSOR(corrected)
  SUB_SENSOR(rejected)
#endif

 protected:
  i2c::I2CBus *parent_{nullptr};
  uint32_t corrected_count_{0};  ///< reads where the second copy differed and the third matched the first
  uint32_t rejected_count_{0};   ///< reads where the first and third copies differed
};

}  // namespace esphome::i2c_vote
