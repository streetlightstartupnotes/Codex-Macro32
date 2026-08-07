// Project publication and maintenance: 路灯同学创业笔记
// X: https://x.com/LDstartupnotes
// Xiaohongshu: https://www.xiaohongshu.com/user/profile/63fd97c1000000001400d0ea
// https://github.com/streetlightstartupnotes

#include <Arduino.h>
#include <BQ27220.h>
#include <Wire.h>

#include "AudioFeedback.h"
#include "CodexMicroBle.h"
#include "CodexUi.h"
#include "Display_ST77916.h"
#include "I2C_Driver.h"
#include "LvglPort.h"

namespace {

constexpr int kBootButton = 0;
constexpr uint32_t kBatteryIntervalMs = 30000;
constexpr uint32_t kUiIntervalMs = 80;

CodexMicroBle codex;
CodexUi ui;
AudioFeedback audio;
BQ27220 gauge;

bool gaugeReady = false;
bool micPressed = false;
uint32_t lastBatteryRead = 0;
uint32_t lastUiUpdate = 0;
int batteryPercent = 100;
bool charging = false;
CodexMicroState previousState;

bool greenDominant(const ThreadLight& light) {
  const int r = (light.color >> 16) & 0xFF;
  const int g = (light.color >> 8) & 0xFF;
  const int b = light.color & 0xFF;
  return light.brightness > 0.08f && g > r * 1.20f && g > b * 1.12f;
}

bool blueDominant(const ThreadLight& light) {
  const int r = (light.color >> 16) & 0xFF;
  const int g = (light.color >> 8) & 0xFF;
  const int b = light.color & 0xFF;
  return light.brightness > 0.08f && b > r * 1.18f && b >= g;
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
  micPressed = stable == LOW;
  codex.sendKey("ACT10", micPressed ? 1 : 0);
  audio.tap();
}

void checkCompletion(const CodexMicroState& state) {
  if (!state.connected || !previousState.connected) return;
  for (size_t i = 0; i < state.threads.size(); ++i) {
    if (blueDominant(previousState.threads[i]) && greenDominant(state.threads[i])) {
      audio.completionChime();
      break;
    }
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

  Backlight_Init();
  Set_Backlight(72);
  LCD_Init();
  LvglPort_Init();

  audio.begin();
  codex.begin();
  updateBattery(true);
  ui.begin(&codex, &audio);
  previousState = codex.snapshot();
  ui.update(previousState, batteryPercent, charging);

  Serial.println("CODEX_MICRO_185B_READY");
}

void loop() {
  updateBootButton();
  updateBattery();

  if (millis() - lastUiUpdate >= kUiIntervalMs) {
    lastUiUpdate = millis();
    CodexMicroState state = codex.snapshot();
    checkCompletion(state);
    ui.update(state, batteryPercent, charging);
    previousState = state;
  }

  LvglPort_Loop();
  delay(5);
}
