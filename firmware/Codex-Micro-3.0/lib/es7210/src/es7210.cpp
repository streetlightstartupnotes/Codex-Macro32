/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "es7210.h"

#include <Arduino.h>
#include <Wire.h>

namespace {

// This is the 7-bit address used by Waveshare's official 1.85B example.
constexpr uint8_t kAddress = 0x40;

constexpr uint8_t kReset = 0x00;
constexpr uint8_t kMainClock = 0x02;
constexpr uint8_t kLrckDividerHigh = 0x04;
constexpr uint8_t kLrckDividerLow = 0x05;
constexpr uint8_t kPowerDown = 0x06;
constexpr uint8_t kOversampling = 0x07;
constexpr uint8_t kTimeControl0 = 0x09;
constexpr uint8_t kTimeControl1 = 0x0A;
constexpr uint8_t kSerialDataFormat = 0x11;
constexpr uint8_t kSerialDataPins = 0x12;
constexpr uint8_t kAdc1Volume = 0x1B;
constexpr uint8_t kAdc2Volume = 0x1C;
constexpr uint8_t kAdc3Volume = 0x1D;
constexpr uint8_t kAdc4Volume = 0x1E;
constexpr uint8_t kAdc34Hpf2 = 0x20;
constexpr uint8_t kAdc34Hpf1 = 0x21;
constexpr uint8_t kAdc12Hpf2 = 0x22;
constexpr uint8_t kAdc12Hpf1 = 0x23;
constexpr uint8_t kAnalogPower = 0x40;
constexpr uint8_t kMic12Bias = 0x41;
constexpr uint8_t kMic34Bias = 0x42;
constexpr uint8_t kMic1Gain = 0x43;
constexpr uint8_t kMic2Gain = 0x44;
constexpr uint8_t kMic3Gain = 0x45;
constexpr uint8_t kMic4Gain = 0x46;
constexpr uint8_t kMic1Power = 0x47;
constexpr uint8_t kMic2Power = 0x48;
constexpr uint8_t kMic3Power = 0x49;
constexpr uint8_t kMic4Power = 0x4A;
constexpr uint8_t kMic12Power = 0x4B;
constexpr uint8_t kMic34Power = 0x4C;

bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kAddress);
  Wire.write(reg);
  Wire.write(value);
  const uint8_t error = Wire.endTransmission(true);
  if (error == 0) return true;
  Serial.printf("ES7210 I2C write failed: reg=0x%02X error=%u\n", reg,
                error);
  return false;
}

bool writeSequence(const uint8_t sequence[][2], size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (!writeRegister(sequence[i][0], sequence[i][1])) return false;
  }
  return true;
}

}  // namespace

bool es7210_begin(uint32_t sampleRateHz) {
  // A 256 x 48 kHz MCLK is the supported 12.288 MHz clock entry used by
  // Waveshare's ES7210 initialization sequence.
  if (sampleRateHz != 48000) {
    Serial.printf("ES7210 unsupported sample rate: %lu Hz\n",
                  static_cast<unsigned long>(sampleRateHz));
    return false;
  }

  Wire.beginTransmission(kAddress);
  if (Wire.endTransmission(true) != 0) {
    Serial.println("ES7210 not found at I2C address 0x40");
    return false;
  }

  // Sequence adapted from the official Waveshare Arduino ES7210 example:
  // 16-bit standard I2S, 2.87 V mic bias, 30 dB analog gain, +18 dB digital
  // gain, high-pass filters enabled, 48 kHz / 12.288 MHz clocking. The extra
  // digital headroom brings the tiny board microphones to a practical macOS
  // input level while remaining below the codec's +32 dB limit.
  static constexpr uint8_t kSequence[][2] = {
      {kReset, 0xFF},
      {kReset, 0x32},
      {kTimeControl0, 0x30},
      {kTimeControl1, 0x30},
      {kAdc12Hpf1, 0x2A},
      {kAdc12Hpf2, 0x0A},
      {kAdc34Hpf1, 0x2A},
      {kAdc34Hpf2, 0x0A},
      {kSerialDataFormat, 0x60},
      {kSerialDataPins, 0x00},
      {kAnalogPower, 0xC3},
      {kMic12Bias, 0x70},
      {kMic34Bias, 0x70},
      {kMic1Gain, 0x1A},
      {kMic2Gain, 0x1A},
      {kMic3Gain, 0x1A},
      {kMic4Gain, 0x1A},
      {kMic1Power, 0x08},
      {kMic2Power, 0x08},
      {kMic3Power, 0x08},
      {kMic4Power, 0x08},
      {kOversampling, 0x20},
      {kMainClock, 0xC1},
      {kLrckDividerHigh, 0x01},
      {kLrckDividerLow, 0x00},
      {kPowerDown, 0x04},
      {kMic12Power, 0x0F},
      {kMic34Power, 0x0F},
      {kAdc1Volume, 0xE3},
      {kAdc2Volume, 0xE3},
      {kAdc3Volume, 0xE3},
      {kAdc4Volume, 0xE3},
      {kReset, 0x71},
      {kReset, 0x41},
  };

  if (!writeSequence(kSequence, sizeof(kSequence) / sizeof(kSequence[0]))) {
    return false;
  }
  delay(5);
  return true;
}
