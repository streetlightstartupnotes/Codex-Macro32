#pragma once

#include <Wire.h>

struct Qmi8658Acceleration {
  float xG = 0.0f;
  float yG = 0.0f;
  float zG = 0.0f;
};

struct Qmi8658AngularVelocity {
  float xDps = 0.0f;
  float yDps = 0.0f;
  float zDps = 0.0f;
};

class Qmi8658 {
 public:
  bool begin(TwoWire& wire, uint8_t preferredAddress = 0x6B);
  bool readAcceleration(Qmi8658Acceleration& acceleration);
  bool readMotion(Qmi8658Acceleration& acceleration,
                  Qmi8658AngularVelocity& angularVelocity);

  uint8_t address() const { return address_; }
  uint8_t revision() const { return revision_; }

 private:
  bool readRegisters(uint8_t reg, uint8_t* data, uint8_t length);
  bool writeRegister(uint8_t reg, uint8_t value);

  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0;
  uint8_t revision_ = 0;
  bool ready_ = false;
};
