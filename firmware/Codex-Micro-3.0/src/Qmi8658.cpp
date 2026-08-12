#include "Qmi8658.h"

#include <Arduino.h>

namespace {

constexpr uint8_t kAddressLow = 0x6A;
constexpr uint8_t kAddressHigh = 0x6B;
constexpr uint8_t kWhoAmIValue = 0x05;

constexpr uint8_t kWhoAmI = 0x00;
constexpr uint8_t kRevision = 0x01;
constexpr uint8_t kCtrl1 = 0x02;
constexpr uint8_t kCtrl2 = 0x03;
constexpr uint8_t kCtrl3 = 0x04;
constexpr uint8_t kCtrl5 = 0x06;
constexpr uint8_t kCtrl7 = 0x08;
constexpr uint8_t kCtrl8 = 0x09;
constexpr uint8_t kStatus0 = 0x2E;
constexpr uint8_t kAccelXLow = 0x35;

// CTRL2: +/-8 g range and 62.5 Hz output data rate.
constexpr uint8_t kAccel8g62Hz = 0x27;
constexpr float kAccelLsbPerG = 4096.0f;
// CTRL3: +/-512 dps range and 62.5 Hz output data rate.
constexpr uint8_t kGyro512Dps62Hz = 0x57;
constexpr float kGyroLsbPerDps = 64.0f;

}  // namespace

bool Qmi8658::begin(TwoWire& wire, uint8_t preferredAddress) {
  wire_ = &wire;
  ready_ = false;
  revision_ = 0;

  const uint8_t alternateAddress =
      preferredAddress == kAddressLow ? kAddressHigh : kAddressLow;
  const uint8_t candidates[2] = {preferredAddress, alternateAddress};
  for (const uint8_t candidate : candidates) {
    if (candidate != kAddressLow && candidate != kAddressHigh) continue;
    address_ = candidate;
    uint8_t whoAmI = 0;
    if (readRegisters(kWhoAmI, &whoAmI, 1) && whoAmI == kWhoAmIValue) {
      break;
    }
    address_ = 0;
  }
  if (address_ == 0) return false;

  // Poll the accelerometer for gravity orientation and the gyroscope for
  // in-plane quarter turns while the round display is nearly flat.
  if (!writeRegister(kCtrl7, 0x00) || !writeRegister(kCtrl1, 0x60) ||
      !writeRegister(kCtrl2, kAccel8g62Hz) ||
      !writeRegister(kCtrl3, kGyro512Dps62Hz) ||
      !writeRegister(kCtrl5, 0x00) || !writeRegister(kCtrl8, 0x00) ||
      !writeRegister(kCtrl7, 0x03)) {
    address_ = 0;
    return false;
  }

  readRegisters(kRevision, &revision_, 1);
  delay(10);
  ready_ = true;
  return true;
}

bool Qmi8658::readAcceleration(Qmi8658Acceleration& acceleration) {
  Qmi8658AngularVelocity ignored;
  return readMotion(acceleration, ignored);
}

bool Qmi8658::readMotion(Qmi8658Acceleration& acceleration,
                         Qmi8658AngularVelocity& angularVelocity) {
  if (!ready_) return false;

  uint8_t status = 0;
  if (!readRegisters(kStatus0, &status, 1) || (status & 0x01) == 0) {
    return false;
  }

  uint8_t bytes[12] = {};
  if (!readRegisters(kAccelXLow, bytes, sizeof(bytes))) return false;

  const int16_t rawX =
      static_cast<int16_t>((static_cast<uint16_t>(bytes[1]) << 8) | bytes[0]);
  const int16_t rawY =
      static_cast<int16_t>((static_cast<uint16_t>(bytes[3]) << 8) | bytes[2]);
  const int16_t rawZ =
      static_cast<int16_t>((static_cast<uint16_t>(bytes[5]) << 8) | bytes[4]);
  const int16_t rawGyroX =
      static_cast<int16_t>((static_cast<uint16_t>(bytes[7]) << 8) | bytes[6]);
  const int16_t rawGyroY =
      static_cast<int16_t>((static_cast<uint16_t>(bytes[9]) << 8) | bytes[8]);
  const int16_t rawGyroZ =
      static_cast<int16_t>((static_cast<uint16_t>(bytes[11]) << 8) | bytes[10]);
  acceleration.xG = static_cast<float>(rawX) / kAccelLsbPerG;
  acceleration.yG = static_cast<float>(rawY) / kAccelLsbPerG;
  acceleration.zG = static_cast<float>(rawZ) / kAccelLsbPerG;
  angularVelocity.xDps = static_cast<float>(rawGyroX) / kGyroLsbPerDps;
  angularVelocity.yDps = static_cast<float>(rawGyroY) / kGyroLsbPerDps;
  angularVelocity.zDps = static_cast<float>(rawGyroZ) / kGyroLsbPerDps;
  return true;
}

bool Qmi8658::readRegisters(uint8_t reg, uint8_t* data, uint8_t length) {
  if (wire_ == nullptr || address_ == 0 || data == nullptr || length == 0) {
    return false;
  }

  wire_->beginTransmission(address_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) return false;

  const size_t received = wire_->requestFrom(address_, length, true);
  if (received != length) {
    while (wire_->available()) wire_->read();
    return false;
  }
  for (uint8_t i = 0; i < length; ++i) {
    if (!wire_->available()) return false;
    data[i] = static_cast<uint8_t>(wire_->read());
  }
  return true;
}

bool Qmi8658::writeRegister(uint8_t reg, uint8_t value) {
  if (wire_ == nullptr || address_ == 0) return false;
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(value);
  return wire_->endTransmission(true) == 0;
}
