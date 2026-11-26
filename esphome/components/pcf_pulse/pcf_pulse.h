#pragma once

#include <atomic>
#include <cstdint>

#include "esphome/components/i2c/i2c_bus.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome::pcf_pulse {

/// Counts falling edges on PCF8574 inputs in its own task, woken by the INT line (see __init__.py).
class PCFPulse final : public Component {
 public:
  void set_bus(i2c::I2CBus *bus) { this->bus_ = bus; }
  void add_address(uint8_t addr) {
    if (this->n_addr_ < 2)
      this->addr_[this->n_addr_++] = addr;
  }
  void set_interrupt_pin(InternalGPIOPin *pin) { this->int_pin_ = pin; }
  void set_poll_ms(uint32_t ms) { this->poll_ms_ = ms < 1 ? 1 : ms; }

  void setup() override;
  void dump_config() override;

  /// Pulses (1 -> 0 edges) on `bit` since the previous call. Main loop only.
  uint32_t take(uint8_t bit);
  /// Diagnostic counters, increasing since boot.
  uint32_t total(uint8_t bit) const { return bit < 16 ? this->count_[bit].load(std::memory_order_relaxed) : 0; }
  uint32_t reads() const { return this->reads_.load(std::memory_order_relaxed); }
  uint32_t irqs() const { return this->irqs_.load(std::memory_order_relaxed); }
  uint32_t errors() const { return this->errors_.load(std::memory_order_relaxed); }
  uint16_t state() const { return this->state_.load(std::memory_order_relaxed); }

 protected:
  static void task_fn(void *arg);
  static void IRAM_ATTR isr(PCFPulse *self);
  void scan_();

  std::atomic<uint32_t> count_[16]{};
  uint32_t taken_[16]{};
  std::atomic<uint32_t> reads_{0};
  std::atomic<uint32_t> irqs_{0};
  std::atomic<uint32_t> errors_{0};
  i2c::I2CBus *bus_{nullptr};
  InternalGPIOPin *int_pin_{nullptr};
  TaskHandle_t task_{nullptr};
  uint32_t poll_ms_{5};
  std::atomic<uint16_t> state_{0xFFFF};
  uint16_t last_{0xFFFF};
  uint8_t addr_[2]{};
  uint8_t n_addr_{0};
  bool have_last_{false};
};

}  // namespace esphome::pcf_pulse
