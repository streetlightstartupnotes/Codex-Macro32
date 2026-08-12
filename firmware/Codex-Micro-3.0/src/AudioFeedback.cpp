#include "AudioFeedback.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>
#include <cmath>

#include "es7210.h"
#include "es8311.h"
#include "UsbMic.h"

namespace {

constexpr int kSampleRate = 48000;
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
  es8311_handle_t outputCodec =
      es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
  if (outputCodec == nullptr) {
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
  if (es8311_init(outputCodec, &clock, ES8311_RESOLUTION_16,
                  ES8311_RESOLUTION_16) != ESP_OK) {
    Serial.println("ES8311 initialization failed; sound disabled");
    return false;
  }
  if (es8311_voice_volume_set(outputCodec, 65, nullptr) != ESP_OK) {
    Serial.println("ES8311 volume setup failed; sound disabled");
    return false;
  }
  if (!es7210_begin(kSampleRate)) {
    Serial.println("ES7210 initialization failed; microphone disabled");
    return false;
  }

  audioI2s.setPins(kBclkPin, kLrclkPin, kDoutPin, kDinPin, kMclkPin);
  audioI2s.setTimeout(1000);
  if (!audioI2s.begin(I2S_MODE_STD, kSampleRate, I2S_DATA_BIT_WIDTH_16BIT,
                      I2S_SLOT_MODE_STEREO)) {
    Serial.println("I2S initialization failed; sound disabled");
    return false;
  }
  // ES7210 supplies the two physical microphones in the left/right I2S
  // slots. Average them in the I2S reader and expose a stable mono stream to
  // the UI and macOS while leaving TX stereo for the ES8311 speaker path.
  if (!audioI2s.configureRX(kSampleRate, I2S_DATA_BIT_WIDTH_16BIT,
                            I2S_SLOT_MODE_STEREO,
                            I2S_RX_TRANSFORM_16_STEREO_TO_MONO)) {
    Serial.println("I2S microphone downmix setup failed; sound disabled");
    return false;
  }
  pinMode(kAmplifierPin, OUTPUT);
  digitalWrite(kAmplifierPin, muted_ ? LOW : HIGH);
  ready_ = true;
  Serial.println(
      "Audio ready: ES8311 output + ES7210 dual mic + I2S + amplifier GPIO9");
  return true;
}

bool AudioFeedback::ready() const { return ready_; }

void AudioFeedback::setMuted(bool muted) {
  if (muted_ == muted) return;
  muted_ = muted;
  if (ready_) digitalWrite(kAmplifierPin, muted_ ? LOW : HIGH);
}

bool AudioFeedback::muted() const { return muted_; }

void AudioFeedback::tone(float hz, int durationMs, float amplitude) {
  if (!ready_ || muted_) return;
  constexpr size_t kChunk = 192;
  int16_t samples[kChunk * 2];
  const size_t total = static_cast<size_t>(kSampleRate * durationMs / 1000);
  size_t written = 0;
  while (written < total) {
#if defined(CODEX_MACRO32_USB_MIC)
    // If macOS starts recording during a task sound, stop at the next small
    // PCM block so the local speaker does not leak into the USB microphone.
    if (codex_usb_mic::streaming()) return;
#endif
    const size_t count = min(kChunk, total - written);
    for (size_t i = 0; i < count; ++i) {
      const float phase = 2.0f * PI * hz * (written + i) / kSampleRate;
      const float edge = min(1.0f, min((written + i) / 120.0f,
                                       (total - written - i) / 120.0f));
      const int16_t sample = static_cast<int16_t>(
          32767.0f * amplitude * edge * sinf(phase));
      samples[i * 2] = sample;
      samples[i * 2 + 1] = sample;
    }
    audioI2s.write(reinterpret_cast<uint8_t*>(samples),
                   count * 2 * sizeof(int16_t));
    written += count;
  }
}

void AudioFeedback::completionChime() {
  if (muted_) return;
  // Major arpeggio C6-E6-G6-C7 with a warm final chord
  tone(1046.5f, 55, 0.16f);
  delay(22);
  tone(1318.5f, 55, 0.18f);
  delay(22);
  tone(1568.0f, 55, 0.20f);
  delay(22);
  tone(2093.0f, 90, 0.24f);
  delay(20);
  // Brief chord: E6+G6 together for richness
  tone(1318.5f, 80, 0.14f);
  delay(8);
  tone(1568.0f, 120, 0.18f);
}

void AudioFeedback::approvalAlert() {
  if (muted_) return;
  // Perfect fifth D5->A5, warm and clear
  tone(587.33f, 75, 0.16f);
  delay(35);
  tone(880.0f, 110, 0.20f);
}

void AudioFeedback::errorAlert() {
  if (muted_) return;
  // Tritone descent: A4 -> Eb4 -> A4 -> Eb4 (tense, attention-grabbing)
  tone(440.0f, 65, 0.18f);
  delay(25);
  tone(311.13f, 80, 0.22f);
  delay(25);
  tone(440.0f, 55, 0.16f);
  delay(20);
  tone(311.13f, 100, 0.24f);
}

void AudioFeedback::readyChime() {
  if (muted_) return;
  // Warm boot: C5 -> G5 -> C6 -> E6 -> G6, expanding major chord
  tone(523.25f, 50, 0.14f);
  delay(20);
  tone(783.99f, 50, 0.16f);
  delay(20);
  tone(1046.5f, 50, 0.18f);
  delay(20);
  tone(1318.5f, 50, 0.20f);
  delay(20);
  tone(1568.0f, 80, 0.22f);
}

float AudioFeedback::microphoneLevel() {
  if (!ready_) return 0.0f;

#if defined(CODEX_MACRO32_USB_MIC)
  // The USB capture task is the sole I2S reader in V3. Reading here as well
  // would steal samples from the Mac stream, so the UI consumes its envelope.
  return microphoneEnvelope_;
#else

  // Eight milliseconds is long enough for a stable energy estimate while
  // keeping the UI responsive. Removing the mean rejects codec DC offset.
  constexpr size_t kSamples = 384;
  int16_t samples[kSamples];
  const size_t bytesRead = audioI2s.readBytes(
      reinterpret_cast<char*>(samples), sizeof(samples));
  const size_t sampleCount = bytesRead / sizeof(int16_t);
  updateMicrophoneEnvelope(samples, sampleCount);
  return microphoneEnvelope_;
#endif
}

size_t AudioFeedback::readMicrophoneSamples(int16_t* samples,
                                            size_t sampleCount) {
  if (!ready_ || samples == nullptr || sampleCount == 0) return 0;
  const size_t bytesRead = audioI2s.readBytes(
      reinterpret_cast<char*>(samples), sampleCount * sizeof(int16_t));
  const size_t samplesRead = bytesRead / sizeof(int16_t);
  updateMicrophoneEnvelope(samples, samplesRead);
  return samplesRead;
}

void AudioFeedback::updateMicrophoneEnvelope(const int16_t* samples,
                                              size_t sampleCount) {
  if (samples == nullptr || sampleCount < 16) {
    microphoneEnvelope_ *= 0.82f;
    return;
  }

  int64_t sum = 0;
  uint64_t sumSquares = 0;
  for (size_t i = 0; i < sampleCount; ++i) {
    const int32_t sample = samples[i];
    sum += sample;
    sumSquares += static_cast<uint64_t>(sample * sample);
  }

  const float mean = static_cast<float>(sum) / sampleCount;
  const float meanSquare = static_cast<float>(sumSquares) / sampleCount;
  const float variance = max(0.0f, meanSquare - mean * mean);
  const float rms = sqrtf(variance) / 32768.0f;

  // Track room noise only when the current window is near the known floor.
  // The nonlinear mapping keeps quiet speech visible without pinning loud
  // speech to the top of the display.
  if (rms < microphoneNoiseFloor_ * 1.8f) {
    microphoneNoiseFloor_ =
        microphoneNoiseFloor_ * 0.985f + rms * 0.015f;
  }
  microphoneNoiseFloor_ =
      constrain(microphoneNoiseFloor_, 0.0004f, 0.08f);
  const float signal = max(0.0f, rms - microphoneNoiseFloor_ * 1.15f);
  float target = constrain(signal * 18.0f, 0.0f, 1.0f);
  target = powf(target, 0.55f);

  const float response = target > microphoneEnvelope_ ? 0.68f : 0.22f;
  microphoneEnvelope_ += (target - microphoneEnvelope_) * response;
}
