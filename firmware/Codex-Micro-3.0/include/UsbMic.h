// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>

class AudioFeedback;

namespace codex_usb_mic {

struct Stats {
  uint64_t capturedSamples;
  uint64_t writtenBytes;
  uint32_t shortWrites;
  uint32_t zeroWrites;
  uint32_t streamTransitions;
  bool ready;
  bool streaming;
};

// Starts a USB Audio Class 1 microphone with a maintenance-only CDC interface.
// Audio is real 48 kHz, 16-bit mono. No USB keyboard, vendor HID or speaker is
// exposed; normal Codex control remains Bluetooth-only.
bool begin(AudioFeedback* audio);
bool ready();
bool streaming();
Stats snapshotStats();

}  // namespace codex_usb_mic
