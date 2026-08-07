#include "AudioFeedback.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>
#include <cmath>

#include "es8311.h"

namespace {

constexpr int kSampleRate = 24000;
constexpr int kMclk = kSampleRate * 256;
constexpr int kBclkPin = 48;
constexpr int kLrclkPin = 38;
constexpr int kDoutPin = 47;
constexpr int kDinPin = 39;
constexpr int kMclkPin = 2;
constexpr int kAmplifierPin = 9;

I2SClass audioI2s;

}  // namespace

bool AudioFeedback::begin() {
  es8311_handle_t codec = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
  if (codec == nullptr) {
    Serial.println("ES8311 not found; sound disabled");
    return false;
  }
  const es8311_clock_config_t clock = {
      .mclk_inverted = false,
      .sclk_inverted = false,
      .mclk_from_mclk_pin = true,
      .mclk_frequency = kMclk,
      .sample_frequency = kSampleRate,
  };
  if (es8311_init(codec, &clock, ES8311_RESOLUTION_16,
                  ES8311_RESOLUTION_16) != ESP_OK) {
    Serial.println("ES8311 initialization failed; sound disabled");
    return false;
  }
  es8311_voice_volume_set(codec, 54, nullptr);
  es8311_microphone_config(codec, false);

  audioI2s.setPins(kBclkPin, kLrclkPin, kDoutPin, kDinPin, kMclkPin);
  if (!audioI2s.begin(I2S_MODE_STD, kSampleRate, I2S_DATA_BIT_WIDTH_16BIT,
                      I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
    Serial.println("I2S initialization failed; sound disabled");
    return false;
  }
  pinMode(kAmplifierPin, OUTPUT);
  digitalWrite(kAmplifierPin, HIGH);
  ready_ = true;
  return true;
}

void AudioFeedback::tone(float hz, int durationMs, float amplitude) {
  if (!ready_) return;
  constexpr size_t kChunk = 192;
  int16_t samples[kChunk];
  const size_t total = static_cast<size_t>(kSampleRate * durationMs / 1000);
  size_t written = 0;
  while (written < total) {
    const size_t count = min(kChunk, total - written);
    for (size_t i = 0; i < count; ++i) {
      const float phase = 2.0f * PI * hz * (written + i) / kSampleRate;
      const float edge = min(1.0f, min((written + i) / 120.0f,
                                       (total - written - i) / 120.0f));
      samples[i] = static_cast<int16_t>(32767.0f * amplitude * edge * sinf(phase));
    }
    audioI2s.write(reinterpret_cast<uint8_t*>(samples), count * sizeof(int16_t));
    written += count;
  }
}

void AudioFeedback::tap() { tone(1046.5f, 28, 0.10f); }

void AudioFeedback::completionChime() {
  tone(880.0f, 85);
  delay(35);
  tone(1318.5f, 125);
}

