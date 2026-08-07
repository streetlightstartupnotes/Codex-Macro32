#pragma once

#include "CodexMicroBle.h"

class AudioFeedback;

class CodexUi {
 public:
  void begin(CodexMicroBle* codex, AudioFeedback* audio);
  void update(const CodexMicroState& state, int batteryPercent, bool charging);

 private:
  CodexMicroBle* codex_ = nullptr;
  AudioFeedback* audio_ = nullptr;
};

