#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "esphome/components/i2c_vote/i2c_vote.h"

namespace esphome::i2c_vote::testing {

/// Answers each read with the next scripted two-byte value and counts the transfers.
class ScriptedBus : public i2c::I2CBus {
 public:
  i2c::ErrorCode write_readv(uint8_t address, const uint8_t *write_buffer, size_t write_count, uint8_t *read_buffer,
                             size_t read_count) override {
    this->transfers++;
    if (read_count == 0)
      return i2c::ERROR_OK;
    if (this->next >= this->answers.size())
      return i2c::ERROR_TIMEOUT;
    const auto &answer = this->answers[this->next++];
    read_buffer[0] = answer[0];
    read_buffer[1] = answer[1];
    return i2c::ERROR_OK;
  }

  std::vector<std::array<uint8_t, 2>> answers;
  size_t next{0};
  int transfers{0};
};

static constexpr uint8_t REG = 0x00;

class I2CVoteTest : public ::testing::Test {
 protected:
  void SetUp() override { this->vote.set_parent(&this->bus); }

  i2c::ErrorCode read_register(uint8_t *out) { return this->vote.write_readv(0x48, &REG, 1, out, 2); }

  ScriptedBus bus;
  I2CVoteBus vote;
};

TEST_F(I2CVoteTest, TwoMatchingReadsPassWithoutAThird) {
  this->bus.answers = {{0x12, 0x30}, {0x12, 0x30}};
  uint8_t out[2]{};
  EXPECT_EQ(this->read_register(out), i2c::ERROR_OK);
  EXPECT_EQ(out[0], 0x12);
  EXPECT_EQ(out[1], 0x30);
  EXPECT_EQ(this->bus.transfers, 2);
}

TEST_F(I2CVoteTest, SecondAndThirdAgreeingIsRejected) {
  // Seen on hardware: a corrupted first copy, then the device stuck returning zeros
  this->bus.answers = {{0x00, 0xB3}, {0x00, 0x00}, {0x00, 0x00}};
  uint8_t out[2]{};
  EXPECT_EQ(this->read_register(out), i2c::ERROR_CRC);
  EXPECT_EQ(this->bus.transfers, 3);
}

TEST_F(I2CVoteTest, BadSecondReadIsOutvoted) {
  this->bus.answers = {{0x12, 0x30}, {0xE3, 0xE3}, {0x12, 0x30}};
  uint8_t out[2]{};
  EXPECT_EQ(this->read_register(out), i2c::ERROR_OK);
  EXPECT_EQ(out[0], 0x12);
  EXPECT_EQ(out[1], 0x30);
  EXPECT_EQ(this->bus.transfers, 3);
}

TEST_F(I2CVoteTest, StuckAfterAGoodFirstReadIsRejected) {
  // Seen on hardware: a good first copy, then ones, then zeros
  this->bus.answers = {{0x48, 0x50}, {0xFF, 0xFF}, {0x00, 0x00}};
  uint8_t out[2]{};
  EXPECT_EQ(this->read_register(out), i2c::ERROR_CRC);
}

TEST_F(I2CVoteTest, ThreeDifferentReadsAreRejected) {
  this->bus.answers = {{0x12, 0x30}, {0xE3, 0xE3}, {0x1F, 0xFF}};
  uint8_t out[2]{};
  EXPECT_EQ(this->read_register(out), i2c::ERROR_CRC);
}

TEST_F(I2CVoteTest, PlainReadIsNotRepeated) {
  this->bus.answers = {{0x12, 0x30}, {0x99, 0x99}};
  uint8_t out[2]{};
  EXPECT_EQ(this->vote.write_readv(0x48, nullptr, 0, out, 2), i2c::ERROR_OK);
  EXPECT_EQ(out[0], 0x12);
  EXPECT_EQ(this->bus.transfers, 1);
}

TEST_F(I2CVoteTest, WriteIsNotRepeated) {
  const uint8_t data[3] = {0x01, 0x85, 0x83};
  EXPECT_EQ(this->vote.write_readv(0x48, data, 3, nullptr, 0), i2c::ERROR_OK);
  EXPECT_EQ(this->bus.transfers, 1);
}

TEST_F(I2CVoteTest, BusErrorIsPassedOn) {
  this->bus.answers = {{0x12, 0x30}};
  uint8_t out[2]{};
  EXPECT_EQ(this->read_register(out), i2c::ERROR_TIMEOUT);
}

}  // namespace esphome::i2c_vote::testing
