#pragma once

#include "CodexMicroBle.h"

class AudioFeedback;

class CodexUi {
 public:
  void begin(CodexMicroBle* codex, AudioFeedback* audio);
  void update(const CodexMicroState& state, int batteryPercent, bool charging);
  void setRecording(bool recording);
  bool takeRecordingStartRequest();
  bool takeRecordingStopRequest(bool* sendAfterStop);
  void wake();
  uint32_t lastInteractionAt() const;

 private:
  CodexMicroBle* codex_ = nullptr;
  AudioFeedback* audio_ = nullptr;
};
