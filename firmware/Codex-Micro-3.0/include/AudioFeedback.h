#pragma once

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

 private:
  bool ready_ = false;
  bool muted_ = false;
  float microphoneNoiseFloor_ = 0.003f;
  float microphoneEnvelope_ = 0.0f;
  void tone(float hz, int durationMs, float amplitude = 0.24f);
};
