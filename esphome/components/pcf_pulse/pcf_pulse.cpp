#include "pcf_pulse.h"
#include "esphome/core/log.h"

namespace esphome::pcf_pulse {

ESPHOME_LOG_TAG(TAG, "pcf_pulse");

void IRAM_ATTR PCFPulse::isr(PCFPulse *self) {
  if (self->task_ == nullptr)
    return;
  self->irqs_.fetch_add(1, std::memory_order_relaxed);
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(self->task_, &woken);
  portYIELD_FROM_ISR(woken);
}

void PCFPulse::setup() {
  // Higher priority than the ESPHome loop task, not pinned to a core
  if (xTaskCreatePinnedToCore(&PCFPulse::task_fn, "pcf_pulse", 3072, this, 5, &this->task_, tskNO_AFFINITY) != pdPASS) {
    this->mark_failed(LOG_STR("Could not create task"));
    return;
  }
  if (this->int_pin_ != nullptr) {
    this->int_pin_->setup();
    this->int_pin_->attach_interrupt(&PCFPulse::isr, this, gpio::INTERRUPT_FALLING_EDGE);
  }
}

void PCFPulse::task_fn(void *arg) {
  auto *self = static_cast<PCFPulse *>(arg);
  // With an interrupt pin only an INT edge wakes the task; without one it polls
  const TickType_t wait = self->int_pin_ != nullptr ? portMAX_DELAY : pdMS_TO_TICKS(self->poll_ms_);
  self->scan_();  // starting state, so the first edge is counted
  for (;;) {
    ulTaskNotifyTake(pdTRUE, wait);
    // INT is released only once the chip that raised it has been read. If the line is still low
    // after a read, an input changed during it, so read again instead of missing that edge.
    for (int i = 0; i < 4; i++) {
      self->scan_();
      if (self->int_pin_ == nullptr || self->int_pin_->digital_read())
        break;
    }
  }
}

void PCFPulse::scan_() {
  uint16_t now = 0;
  for (uint8_t i = 0; i < this->n_addr_; i++) {
    uint8_t b;
    if (this->bus_->read(this->addr_[i], &b, 1) != i2c::ERROR_OK) {
      this->errors_.fetch_add(1, std::memory_order_relaxed);
      return;  // compare complete reads only
    }
    now |= uint16_t(b) << (8 * i);
  }
  if (this->n_addr_ == 1)
    now |= 0xFF00;
  this->reads_.fetch_add(1, std::memory_order_relaxed);
  this->state_.store(now, std::memory_order_relaxed);
  if (!this->have_last_) {
    this->last_ = now;
    this->have_last_ = true;
    return;
  }
  uint16_t fell = this->last_ & ~now;
  this->last_ = now;
  while (fell) {
    int bit = __builtin_ctz(fell);
    this->count_[bit].fetch_add(1, std::memory_order_relaxed);
    fell &= fell - 1;
  }
}

uint32_t PCFPulse::take(uint8_t bit) {
  if (bit >= 16)
    return 0;
  uint32_t c = this->count_[bit].load(std::memory_order_relaxed);
  uint32_t d = c - this->taken_[bit];
  this->taken_[bit] = c;
  return d;
}

void PCFPulse::dump_config() {
  ESP_LOGCONFIG(TAG, "PCF pulse:");
  for (uint8_t i = 0; i < this->n_addr_; i++)
    ESP_LOGCONFIG(TAG, "  Chip %u: 0x%02X (bits %u..%u)", i, this->addr_[i], 8 * i, 8 * i + 7);
  LOG_PIN("  Interrupt pin: ", this->int_pin_);
  if (this->int_pin_ == nullptr)
    ESP_LOGCONFIG(TAG, "  Poll interval: %" PRIu32 " ms", this->poll_ms_);
}

}  // namespace esphome::pcf_pulse
