#pragma once

class AudioFeedback {
 public:
  bool begin();
  void completionChime();
  void tap();

 private:
  bool ready_ = false;
  void tone(float hz, int durationMs, float amplitude = 0.24f);
};

