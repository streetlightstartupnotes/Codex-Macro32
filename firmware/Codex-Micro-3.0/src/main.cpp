// Project publication and maintenance: 路灯同学创业笔记
// X: https://x.com/LDstartupnotes
// Xiaohongshu: https://www.xiaohongshu.com/user/profile/63fd97c1000000001400d0ea
// https://github.com/streetlightstartupnotes

#include <Arduino.h>
#include <BQ27220.h>
#include <Wire.h>
#include <cmath>

#include "AudioFeedback.h"
#include "CodexMicroBle.h"
#include "CodexUi.h"
#include "Display_ST77916.h"
#include "I2C_Driver.h"
#include "LvglPort.h"
#include "Qmi8658.h"
#include "UsbMic.h"

namespace {

constexpr int kBootButton = 0;
constexpr uint32_t kBatteryIntervalMs = 30000;
constexpr uint32_t kUiIntervalMs = 80;
constexpr uint8_t kBacklightAwake = 72;
constexpr float kBacklightMaximum = 100.0f;
constexpr uint32_t kImuIntervalMs = 40;
constexpr float kImuFilterAlpha = 0.35f;
constexpr float kMotionReferenceEnterG = 0.20f;
constexpr float kMotionStepEnterG = 0.10f;
constexpr float kMotionNormEnterG = 0.16f;
constexpr float kMotionReferenceReleaseG = 0.07f;
constexpr float kMotionStepReleaseG = 0.035f;
constexpr float kMotionNormReleaseG = 0.10f;
constexpr uint8_t kMotionDebounceSamples = 2;
constexpr uint32_t kMotionQuietRearmMs = 500;
constexpr uint32_t kMotionWakeCooldownMs = 1000;
constexpr float kFaceDownEnterZG = -0.78f;
constexpr float kFaceUpExitZG = 0.60f;
constexpr float kOrientationNormMinG = 0.80f;
constexpr float kOrientationNormMaxG = 1.20f;
constexpr uint8_t kOrientationDebounceSamples = 15;

CodexMicroBle codex;
CodexUi ui;
AudioFeedback audio;
BQ27220 gauge;
Qmi8658 imu;

struct ImuInteractionState {
  bool hasSample = false;
  bool motionArmed = true;
  Qmi8658Acceleration filtered;
  Qmi8658Acceleration previous;
  Qmi8658Acceleration motionReference;
  uint8_t motionSamples = 0;
  uint32_t quietSince = 0;
  uint32_t lastWakeAt = 0;
  int8_t faceCandidate = 0;
  uint8_t faceSamples = 0;
};

bool gaugeReady = false;
bool imuReady = false;
bool micPressed = false;
bool micLatched = false;
bool suppressNextBootRelease = false;
bool recordingActive = false;
uint32_t lastBootReleaseAt = 0;
uint32_t lastBatteryRead = 0;
uint32_t lastUiUpdate = 0;
uint32_t lastImuRead = 0;
uint8_t backlightLevel = kBacklightAwake;
uint8_t backlightTarget = kBacklightAwake;
int batteryPercent = 100;
bool charging = false;
bool faceDownMuted = false;
bool companionSoundEnabled = true;
CodexMicroState previousState;
ImuInteractionState imuState;

enum class PendingAlert : uint8_t {
  None = 0,
  Completion = 1,
  Approval = 2,
  Error = 3,
};

PendingAlert pendingAlert = PendingAlert::None;

void applyAudioMute() {
  const bool shouldMute = faceDownMuted || !companionSoundEnabled;
  if (audio.muted() != shouldMute) {
    Serial.printf("Audio %s (%s)\n", shouldMute ? "muted" : "enabled",
                  faceDownMuted ? "face down" : "companion setting");
  }
  audio.setMuted(shouldMute);
}

void setBacklight(uint8_t level) {
  if (backlightLevel == level) return;
  backlightLevel = level;
  Set_Backlight(level);
}

uint8_t backlightForState(const CodexMicroState& state) {
  if (!state.connected || !state.lightingReady) return kBacklightAwake;
  const float brightness = constrain(state.lightingBrightness, 0.0f, 1.0f);
  return static_cast<uint8_t>(lroundf(brightness * kBacklightMaximum));
}

void syncBacklight(const CodexMicroState& state) {
  const uint8_t nextTarget = backlightForState(state);
  if (nextTarget != backlightTarget) {
    Serial.printf("Codex lighting backlight=%u%%\n", nextTarget);
  }
  backlightTarget = nextTarget;
  setBacklight(backlightTarget);
}

void wakeDisplay() {
  // ChatGPT owns the synchronized brightness. A local touch, recording event
  // or motion must not override an active Codex Auto-dim command; HID activity
  // will make ChatGPT send the configured brightness again.
  setBacklight(backlightTarget);
  ui.wake();
}

void updateBacklight() {
  setBacklight(backlightTarget);
}

float accelerationLength(const Qmi8658Acceleration& value) {
  return sqrtf(value.xG * value.xG + value.yG * value.yG +
               value.zG * value.zG);
}

float accelerationDistance(const Qmi8658Acceleration& a,
                           const Qmi8658Acceleration& b) {
  const float dx = a.xG - b.xG;
  const float dy = a.yG - b.yG;
  const float dz = a.zG - b.zG;
  return sqrtf(dx * dx + dy * dy + dz * dz);
}

void updateMotionWake(uint32_t now) {
  const float stepDelta =
      accelerationDistance(imuState.filtered, imuState.previous);
  const float referenceDelta =
      accelerationDistance(imuState.filtered, imuState.motionReference);
  const float normError = fabsf(accelerationLength(imuState.filtered) - 1.0f);
  const bool motionHigh = referenceDelta >= kMotionReferenceEnterG ||
                          stepDelta >= kMotionStepEnterG ||
                          normError >= kMotionNormEnterG;
  const bool motionQuiet = stepDelta <= kMotionStepReleaseG &&
                           normError <= kMotionNormReleaseG;

  if (imuState.motionArmed) {
    if (motionHigh) {
      if (imuState.motionSamples < UINT8_MAX) ++imuState.motionSamples;
    } else {
      imuState.motionSamples = 0;
    }

    if (imuState.motionSamples >= kMotionDebounceSamples) {
      if (imuState.lastWakeAt == 0 ||
          now - imuState.lastWakeAt >= kMotionWakeCooldownMs) {
        // IMU movement is deliberately limited to this harmless UI action.
        wakeDisplay();
        imuState.lastWakeAt = now;
      }
      imuState.motionArmed = false;
      imuState.motionSamples = 0;
      imuState.quietSince = 0;
      return;
    }

    if (motionQuiet && referenceDelta <= kMotionReferenceReleaseG) {
      constexpr float kReferenceDriftAlpha = 0.04f;
      imuState.motionReference.xG +=
          kReferenceDriftAlpha *
          (imuState.filtered.xG - imuState.motionReference.xG);
      imuState.motionReference.yG +=
          kReferenceDriftAlpha *
          (imuState.filtered.yG - imuState.motionReference.yG);
      imuState.motionReference.zG +=
          kReferenceDriftAlpha *
          (imuState.filtered.zG - imuState.motionReference.zG);
    }
    return;
  }

  if (!motionQuiet) {
    imuState.quietSince = 0;
    return;
  }
  if (imuState.quietSince == 0) {
    imuState.quietSince = now;
    return;
  }
  if (now - imuState.quietSince >= kMotionQuietRearmMs) {
    imuState.motionReference = imuState.filtered;
    imuState.motionArmed = true;
    imuState.quietSince = 0;
  }
}

void updateFaceDownMute() {
  const float norm = accelerationLength(imuState.filtered);
  if (norm < kOrientationNormMinG || norm > kOrientationNormMaxG) {
    imuState.faceCandidate = 0;
    imuState.faceSamples = 0;
    return;
  }

  int8_t candidate = 0;
  // Waveshare's reference calibration treats display-up as +Z. The former
  // polarity muted the speaker in its normal face-up position.
  if (!faceDownMuted && imuState.filtered.zG <= kFaceDownEnterZG) {
    candidate = 1;
  } else if (faceDownMuted && imuState.filtered.zG >= kFaceUpExitZG) {
    candidate = -1;
  }

  if (candidate == 0) {
    imuState.faceCandidate = 0;
    imuState.faceSamples = 0;
    return;
  }
  if (candidate != imuState.faceCandidate) {
    imuState.faceCandidate = candidate;
    imuState.faceSamples = 1;
    return;
  }
  if (imuState.faceSamples < UINT8_MAX) {
    ++imuState.faceSamples;
  }
  if (imuState.faceSamples < kOrientationDebounceSamples) return;

  faceDownMuted = candidate > 0;
  applyAudioMute();
  Serial.printf("IMU audio %s\n", faceDownMuted ? "muted" : "unmuted");
  imuState.faceCandidate = 0;
  imuState.faceSamples = 0;
}

void updateImu() {
  const uint32_t now = millis();
  if (!imuReady || now - lastImuRead < kImuIntervalMs) return;
  lastImuRead = now;

  Qmi8658Acceleration sample;
  if (!imu.readAcceleration(sample)) return;

  if (!imuState.hasSample) {
    imuState.hasSample = true;
    imuState.filtered = sample;
    imuState.previous = sample;
    imuState.motionReference = sample;
  } else {
    imuState.previous = imuState.filtered;
    imuState.filtered.xG +=
        kImuFilterAlpha * (sample.xG - imuState.filtered.xG);
    imuState.filtered.yG +=
        kImuFilterAlpha * (sample.yG - imuState.filtered.yG);
    imuState.filtered.zG +=
        kImuFilterAlpha * (sample.zG - imuState.filtered.zG);
  }

  updateMotionWake(now);
  updateFaceDownMute();
  // IMU is deliberately limited to motion wake and face-down mute.
}

bool greenDominant(const ThreadLight& light) {
  const int r = (light.color >> 16) & 0xFF;
  const int g = (light.color >> 8) & 0xFF;
  const int b = light.color & 0xFF;
  return light.color != 0 && g > r * 1.20f && g > b * 1.12f;
}

bool blueDominant(const ThreadLight& light) {
  const int r = (light.color >> 16) & 0xFF;
  const int g = (light.color >> 8) & 0xFF;
  const int b = light.color & 0xFF;
  return light.color != 0 && b > r * 1.18f && b >= g;
}

bool yellowDominant(const ThreadLight& light) {
  const int r = (light.color >> 16) & 0xFF;
  const int g = (light.color >> 8) & 0xFF;
  const int b = light.color & 0xFF;
  return light.color != 0 && r > 150 && g > 90 && b < 120;
}

bool redDominant(const ThreadLight& light) {
  const int r = (light.color >> 16) & 0xFF;
  const int g = (light.color >> 8) & 0xFF;
  return light.color != 0 && !yellowDominant(light) && r > 150 &&
         r > g * 1.35f;
}

void sendMomentary(const char* key) {
  codex.sendKey(key, 1);
  delay(18);
  codex.sendKey(key, 0);
}

void startRecording() {
  if (recordingActive) return;
  recordingActive = true;
  wakeDisplay();
  ui.setRecording(true);
  codex.sendKey("ACT10", 1);
}

void stopRecording(bool sendAfterStop, bool forceRelease = false) {
  const bool wasRecording = recordingActive;
  recordingActive = false;
  wakeDisplay();
  ui.setRecording(false);
  if (wasRecording || forceRelease) codex.sendKey("ACT10", 0);
  if (sendAfterStop) sendMomentary("ACT12");
}

void updateBattery(bool force = false) {
  if (!force && millis() - lastBatteryRead < kBatteryIntervalMs) return;
  lastBatteryRead = millis();
  if (gaugeReady) {
    const int value = gauge.readStateOfChargePercent();
    const int current = gauge.readAverageCurrentMilliamps();
    if (value >= 0 && value <= 100) batteryPercent = value;
    if (current != INT16_MIN) charging = current > 8;
  }
  codex.setBattery(batteryPercent, charging);
}

void updateBootButton() {
  static bool lastRaw = HIGH;
  static bool stable = HIGH;
  static uint32_t changedAt = 0;
  const bool raw = digitalRead(kBootButton);
  if (raw != lastRaw) {
    lastRaw = raw;
    changedAt = millis();
  }
  if (millis() - changedAt < 18 || raw == stable) return;
  stable = raw;
  const uint32_t now = millis();

  if (stable == LOW) {
    micPressed = true;
    if (micLatched) {
      // A tap while hands-free recording is active stops it immediately.
      micLatched = false;
      suppressNextBootRelease = true;
      lastBootReleaseAt = 0;
      stopRecording(false);
    } else if (lastBootReleaseAt != 0 &&
               now - lastBootReleaseAt <= 350) {
      // The second tap restarts Mic and keeps it active after release.
      micLatched = true;
      lastBootReleaseAt = 0;
      startRecording();
    } else {
      startRecording();
    }
    return;
  }

  micPressed = false;
  if (suppressNextBootRelease) {
    suppressNextBootRelease = false;
    return;
  }
  if (micLatched) return;

  stopRecording(false);
  lastBootReleaseAt = now;
}

void handleRecordingStopRequest() {
  bool sendAfterStop = false;
  if (!ui.takeRecordingStopRequest(&sendAfterStop)) return;

  micLatched = false;
  suppressNextBootRelease = micPressed;
  lastBootReleaseAt = 0;
  stopRecording(sendAfterStop, true);
}

void handleRecordingStartRequest() {
  if (!ui.takeRecordingStartRequest()) return;
  micLatched = true;
  lastBootReleaseAt = 0;
  startRecording();
}

void checkStateTransitions(const CodexMicroState& state) {
  bool playCompletion = false;
  bool playApproval = false;
  bool playError = false;
  if (state.connected && previousState.connected) {
    for (size_t i = 0; i < state.threads.size(); ++i) {
      const ThreadLight& before = previousState.threads[i];
      const ThreadLight& after = state.threads[i];
      const bool enteredComplete =
          greenDominant(after) && !greenDominant(before);
      playCompletion = playCompletion || enteredComplete;
      playApproval = playApproval ||
                     (yellowDominant(after) && !yellowDominant(before));
      playError = playError || (redDominant(after) && !redDominant(before));
    }
  }

  PendingAlert detected = PendingAlert::None;
  if (playError) {
    detected = PendingAlert::Error;
  } else if (playApproval) {
    detected = PendingAlert::Approval;
  } else if (playCompletion) {
    detected = PendingAlert::Completion;
  }
  if (detected != PendingAlert::None) {
    wakeDisplay();
    if (static_cast<uint8_t>(detected) >
        static_cast<uint8_t>(pendingAlert)) {
      pendingAlert = detected;
    }
  }

  // The ES8311 speaker and ES7210 microphones share the I2S clock. A task
  // sound while macOS records could enter the USB stream. Preserve the
  // highest-priority event and play it after the host releases the microphone.
  if (codex_usb_mic::streaming()) return;

  const PendingAlert alert = pendingAlert;
  pendingAlert = PendingAlert::None;
  if (alert == PendingAlert::Error) {
    audio.errorAlert();
  } else if (alert == PendingAlert::Approval) {
    audio.approvalAlert();
  } else if (alert == PendingAlert::Completion) {
    audio.completionChime();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("Codex Micro 1.85B boot");

  pinMode(kBootButton, INPUT_PULLUP);
  I2C_Init();
  gaugeReady = gauge.begin(Wire, 0x55, -1, -1, 400000);
  imuReady = imu.begin(Wire);
  if (imuReady) {
    Serial.printf("QMI8658 ready at 0x%02X, revision 0x%02X\n", imu.address(),
                  imu.revision());
  } else {
    Serial.println("QMI8658 not found; motion features disabled");
  }

  Backlight_Init();
  Set_Backlight(kBacklightAwake);
  backlightLevel = kBacklightAwake;
  backlightTarget = kBacklightAwake;
  LCD_Init();
  LvglPort_Init();

  const bool audioReady = audio.begin();
  codex.begin();
  updateBattery(true);
  ui.begin(&codex, &audio);
  previousState = codex.snapshot();
  companionSoundEnabled = previousState.companionSoundEnabled;
  applyAudioMute();
  ui.update(previousState, batteryPercent, charging);

  if (audioReady && !audio.muted()) audio.readyChime();

#if defined(CODEX_MACRO32_USB_MIC)
  if (!codex_usb_mic::begin(&audio)) {
    Serial.println("USB_MIC_FAILED");
  }
#endif

  Serial.println("CODEX_MACRO32_V3_READY");
}

void loop() {
  codex.poll();
  updateBootButton();
  handleRecordingStartRequest();
  handleRecordingStopRequest();
  updateBattery();
  updateImu();

  if (millis() - lastUiUpdate >= kUiIntervalMs) {
    lastUiUpdate = millis();
    CodexMicroState state = codex.snapshot();
    if (companionSoundEnabled != state.companionSoundEnabled) {
      companionSoundEnabled = state.companionSoundEnabled;
      applyAudioMute();
    }
    syncBacklight(state);
    checkStateTransitions(state);
    ui.update(state, batteryPercent, charging);
    previousState = state;
  }

  LvglPort_Loop();
  updateBacklight();
  delay(5);
}
