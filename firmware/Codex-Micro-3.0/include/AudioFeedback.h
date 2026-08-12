#pragma once

#include <stddef.h>
#include <stdint.h>

class AudioFeedback {
 public:
  bool begin();
  bool ready() const;
  void setMuted(bool muted);
  bool muted() const;
  void completionChime();
  void approvalAlert();
  void errorAlert();
  void readyChime();
  float microphoneLevel();
  size_t readMicrophoneSamples(int16_t* samples, size_t sampleCount);

 private:
  bool ready_ = false;
  bool muted_ = false;
  float microphoneNoiseFloor_ = 0.003f;
  volatile float microphoneEnvelope_ = 0.0f;
  void updateMicrophoneEnvelope(const int16_t* samples, size_t sampleCount);
  void tone(float hz, int durationMs, float amplitude = 0.24f);
};
