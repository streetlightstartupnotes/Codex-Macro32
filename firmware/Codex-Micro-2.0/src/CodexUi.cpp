// Project publication and maintenance: 路灯同学创业笔记
// X: https://x.com/LDstartupnotes
// Xiaohongshu: https://www.xiaohongshu.com/user/profile/63fd97c1000000001400d0ea
// https://github.com/streetlightstartupnotes

#include "CodexUi.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cmath>

#include "AudioFeedback.h"

LV_FONT_DECLARE(lv_font_codex_ui_16);

namespace {

constexpr int kCenter = 180;
constexpr int kAgentOrbit = 132;
constexpr int kAgentSize = 86;
constexpr int kUsageSize = 176;
constexpr int kWaveBars = 29;
constexpr int kWaveStartX = 68;
constexpr int kWaveStepX = 8;
constexpr int kWaveCenterY = 180;

const lv_color_t kBg = LV_COLOR_MAKE(0x00, 0x00, 0x00);
constexpr uint32_t kPanel = 0x061725;
constexpr uint32_t kPanelRaised = 0x0A2234;
constexpr uint32_t kCyan = 0x28E7FF;
constexpr uint32_t kBlue = 0x228BFF;
constexpr uint32_t kIce = 0xEAFBFF;
constexpr uint32_t kDim = 0x174158;
constexpr uint32_t kDimmer = 0x0B293B;
constexpr uint32_t kRed = 0xFF4E67;
constexpr uint32_t kGreen = 0x35E58A;
constexpr uint32_t kYellow = 0xFFD166;

CodexMicroBle* transport = nullptr;
AudioFeedback* sound = nullptr;
lv_obj_t* agentButtons[6] = {};
lv_obj_t* agentLabels[6] = {};
lv_obj_t* centerDisk = nullptr;
lv_obj_t* usageArc = nullptr;
lv_obj_t* usageDividers[4] = {};
lv_obj_t* centerHit = nullptr;
lv_obj_t* connectionDot = nullptr;
lv_obj_t* usageLabel = nullptr;
lv_obj_t* resetLabel = nullptr;
lv_obj_t* pageLabel = nullptr;
lv_obj_t* recordTimeLabel = nullptr;
lv_obj_t* batteryLabel = nullptr;
lv_obj_t* batteryIcon = nullptr;
lv_obj_t* batteryCap = nullptr;
lv_obj_t* batteryFill = nullptr;
lv_obj_t* detailTitleLabel = nullptr;
lv_obj_t* detailMetaLabel = nullptr;
lv_obj_t* waveform[kWaveBars] = {};
float waveformLevels[kWaveBars] = {};
lv_obj_t* navRing = nullptr;
lv_obj_t* navArcs[4] = {};
lv_obj_t* navLabels[4] = {};
lv_obj_t* navCenter = nullptr;
lv_obj_t* navCenterLabel = nullptr;
lv_obj_t* navSurface = nullptr;

bool commandPage = false;
bool joystickPage = false;
bool pairResetTriggered = false;
bool micRecording = false;
bool centerReleaseHandled = false;
uint32_t centerPressedAt = 0;
uint8_t centerLongPressStage = 0;
bool ignoreNextNavCenterClick = false;
uint32_t recordingStartedAt = 0;
uint32_t uiLastInteractionAt = 0;
bool recordingStopRequested = false;
bool recordingSendAfterStop = false;
bool recordingStartRequested = false;
int detailAgent = -1;
uint32_t detailUntil = 0;
int renderedDetailAgent = -1;
String renderedDetailTitleSource;
String renderedDetailWorkspaceSource;
bool suppressAgentClick[6] = {};
constexpr uint32_t activePrimary = kCyan;
constexpr uint32_t activeSecondary = kBlue;
uint32_t displayedAgentAccent[6] = {};
bool agentAccentInitialized[6] = {};

const char* kCommandNames[6] = {"快速", "同意", "拒绝", "分支", "录音", "发送"};
const char* kCommandIds[6] = {"ACT06", "ACT07", "ACT08", "ACT09", "ACT10", "ACT12"};

uint32_t scaleColor(uint32_t raw, float brightness);

uint32_t blendColor(uint32_t from, uint32_t to, float amount) {
  amount = constrain(amount, 0.0f, 1.0f);
  const uint8_t fromR = (from >> 16) & 0xFF;
  const uint8_t fromG = (from >> 8) & 0xFF;
  const uint8_t fromB = from & 0xFF;
  const uint8_t toR = (to >> 16) & 0xFF;
  const uint8_t toG = (to >> 8) & 0xFF;
  const uint8_t toB = to & 0xFF;
  const uint8_t r = fromR + static_cast<int>((toR - fromR) * amount);
  const uint8_t g = fromG + static_cast<int>((toG - fromG) * amount);
  const uint8_t b = fromB + static_cast<int>((toB - fromB) * amount);
  return (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | b;
}

void zoomAnimExec(void* object, int32_t value) {
  lv_obj_set_style_transform_zoom(static_cast<lv_obj_t*>(object), value, 0);
}

void animateZoom(lv_obj_t* object, int32_t target, uint32_t duration) {
  if (object == nullptr) return;
  lv_anim_del(object, zoomAnimExec);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, object);
  lv_anim_set_exec_cb(&animation, zoomAnimExec);
  lv_anim_set_values(&animation,
                     lv_obj_get_style_transform_zoom(object, LV_PART_MAIN),
                     target);
  lv_anim_set_time(&animation, duration);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_start(&animation);
}

void markInteraction() {
  uiLastInteractionAt = millis();
}

const char* threadStatus(const ThreadLight& light) {
  if (light.color == 0 || light.brightness <= 0.01f) return "UNASSIGNED";
  const uint8_t r = (light.color >> 16) & 0xFF;
  const uint8_t g = (light.color >> 8) & 0xFF;
  const uint8_t b = light.color & 0xFF;
  if (r > 150 && g > 90 && b < 120) return "WAITING";
  if (r > 150 && r > g * 1.35f) return "ERROR";
  if (g > r * 1.15f && g > b * 1.08f) return "COMPLETE";
  if (light.effect == "breath" || (b > r * 1.15f && b >= g)) return "RUNNING";
  return "IDLE";
}

const char* localizedStatus(const char* status) {
  if (strcmp(status, "ERROR") == 0) return "错误";
  if (strcmp(status, "WAITING") == 0) return "待处理";
  if (strcmp(status, "COMPLETE") == 0) return "已完成";
  if (strcmp(status, "RUNNING") == 0) return "运行中";
  if (strcmp(status, "IDLE") == 0) return "空闲";
  return "未分配";
}

String safeTextForDisplay(const String& source, const char* fallback) {
  if (source.isEmpty()) return String(fallback);

  const char* text = source.c_str();
  uint32_t offset = 0;
  while (text[offset] != '\0') {
    uint32_t consumed = 0;
    const uint32_t codepoint = _lv_txt_encoded_next(text + offset, &consumed);
    if (consumed == 0) break;

    lv_font_glyph_dsc_t glyph;
    const bool supported = lv_font_get_glyph_dsc(
        &lv_font_codex_ui_16, &glyph, codepoint, 0);
    if (!supported) return String(fallback);
    offset += consumed;
  }
  return source;
}

void formatDuration(char* output, size_t size, uint32_t seconds) {
  if (seconds >= 86400) {
    snprintf(output, size, "%lu天%02lu时",
             static_cast<unsigned long>(seconds / 86400),
             static_cast<unsigned long>((seconds % 86400) / 3600));
  } else if (seconds >= 3600) {
    snprintf(output, size, "%lu时%02lu分",
             static_cast<unsigned long>(seconds / 3600),
             static_cast<unsigned long>((seconds % 3600) / 60));
  } else {
    snprintf(output, size, "%lu分", static_cast<unsigned long>(seconds / 60));
  }
}

uint32_t scaleColor(uint32_t raw, float brightness) {
  if (raw == 0 || brightness <= 0.01f) return 0xB9C8EC;
  const float factor = constrain(brightness, 0.28f, 1.0f);
  const uint8_t r = static_cast<uint8_t>(((raw >> 16) & 0xFF) * factor);
  const uint8_t g = static_cast<uint8_t>(((raw >> 8) & 0xFF) * factor);
  const uint8_t b = static_cast<uint8_t>((raw & 0xFF) * factor);
  return (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | b;
}

uint32_t readableTextColor(uint32_t background) {
  const uint8_t r = (background >> 16) & 0xFF;
  const uint8_t g = (background >> 8) & 0xFF;
  const uint8_t b = background & 0xFF;
  const uint16_t luminance = static_cast<uint16_t>(r) * 3 +
                             static_cast<uint16_t>(g) * 6 + b;
  return luminance > 1120 ? 0x021015 : kIce;
}

void sendMomentary(const char* id, int agent = -1) {
  if (transport == nullptr) return;
  transport->sendKey(id, 1, agent);
  delay(18);
  transport->sendKey(id, 0, agent);
}

void sendAgent(int index) {
  char id[5];
  snprintf(id, sizeof(id), "AG%02d", index + 1);
  sendMomentary(id, index);
}

lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, const lv_font_t* font,
                    lv_color_t color) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  return label;
}

void setAgentCaption(int index, const char* text) {
  lv_label_set_text(agentLabels[index], text);
  lv_obj_center(agentLabels[index]);
}

void setObjectHidden(lv_obj_t* object, bool hidden) {
  if (object == nullptr) return;
  if (hidden) {
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
  }
}

void setHomeUiHidden(bool hidden) {
  for (lv_obj_t* button : agentButtons) setObjectHidden(button, hidden);
  setObjectHidden(centerDisk, hidden);
  setObjectHidden(usageArc, hidden);
  for (lv_obj_t* divider : usageDividers) setObjectHidden(divider, hidden);
  setObjectHidden(centerHit, hidden);
  setObjectHidden(connectionDot, hidden);
  setObjectHidden(usageLabel, hidden);
  setObjectHidden(resetLabel, hidden);
  setObjectHidden(pageLabel, hidden);
  if (hidden) {
    setObjectHidden(detailTitleLabel, true);
    setObjectHidden(detailMetaLabel, true);
    setObjectHidden(batteryLabel, true);
    setObjectHidden(batteryIcon, true);
    setObjectHidden(batteryCap, true);
    setObjectHidden(batteryFill, true);
  }
}

uint32_t quotaAccent(int weeklyLeft) {
  if (weeklyLeft >= 50) return kGreen;
  if (weeklyLeft >= 20) return kYellow;
  return kRed;
}

void updateBatteryVisual(int batteryPercent, bool charging) {
  const int value = constrain(batteryPercent, 0, 100);
  lv_obj_set_style_border_color(batteryIcon,
                                lv_color_hex(charging ? kCyan : kDim), 0);
  lv_obj_set_style_bg_color(batteryCap,
                            lv_color_hex(charging ? kCyan : kDim), 0);
  lv_obj_set_style_bg_color(batteryFill,
                            lv_color_hex(value <= 15 ? kRed : kCyan), 0);
  lv_obj_set_width(batteryFill, max(2, (value * 28) / 100));
  char text[8];
  snprintf(text, sizeof(text), "%d%%", value);
  lv_label_set_text(batteryLabel, text);
}

void setJoystickUiHidden(bool hidden) {
  setObjectHidden(navRing, hidden);
  for (lv_obj_t* arc : navArcs) setObjectHidden(arc, hidden);
  for (lv_obj_t* label : navLabels) setObjectHidden(label, hidden);
  setObjectHidden(navCenter, hidden);
  setObjectHidden(navCenterLabel, hidden);
  setObjectHidden(navSurface, hidden);
}

void setCommandPage(bool enabled, bool playSound = true) {
  commandPage = enabled;
  detailAgent = -1;
  renderedDetailAgent = -1;
  if (detailTitleLabel != nullptr) {
    lv_obj_add_flag(detailTitleLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(detailMetaLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(usageLabel, LV_OBJ_FLAG_HIDDEN);
  }
  markInteraction();

  if (enabled) {
    lv_label_set_text(pageLabel, "快捷操作");
    lv_label_set_text(usageLabel, "6");
    lv_label_set_text(resetLabel, "点击执行");
    lv_arc_set_value(usageArc, 100);
  } else {
    lv_label_set_text(pageLabel, "");
  }

  for (int i = 0; i < 6; ++i) {
    if (enabled) {
      lv_obj_set_style_text_font(agentLabels[i], &lv_font_codex_ui_16, 0);
      setAgentCaption(i, kCommandNames[i]);
      lv_obj_set_style_bg_color(agentButtons[i], lv_color_hex(kPanelRaised), 0);
      lv_obj_set_style_bg_grad_color(agentButtons[i], lv_color_hex(kPanel), 0);
      lv_obj_set_style_bg_grad_dir(agentButtons[i], LV_GRAD_DIR_VER, 0);
      lv_obj_set_style_border_color(agentButtons[i], lv_color_hex(activePrimary), 0);
      lv_obj_set_style_shadow_color(agentButtons[i], lv_color_hex(activePrimary), 0);
      lv_obj_set_style_shadow_opa(agentButtons[i], LV_OPA_30, 0);
      lv_obj_set_style_text_color(agentLabels[i], lv_color_hex(kIce), 0);
    } else {
      lv_obj_set_style_text_font(agentLabels[i], &lv_font_montserrat_16, 0);
      char label[4];
      snprintf(label, sizeof(label), "%d", i + 1);
      setAgentCaption(i, label);
    }
  }
  (void)playSound;
}

void setJoystickPage(bool enabled, bool playSound = true) {
  if (enabled) {
    setCommandPage(false, false);
    joystickPage = true;
    setHomeUiHidden(true);
    setJoystickUiHidden(false);
    lv_obj_move_foreground(navSurface);
  } else {
    joystickPage = false;
    ignoreNextNavCenterClick = false;
    setJoystickUiHidden(true);
    setHomeUiHidden(false);
    lv_obj_add_flag(detailTitleLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(detailMetaLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(usageLabel, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(pageLabel, "");
  }
  markInteraction();
  (void)playSound;
}

void setRecordingVisual(bool enabled) {
  if (enabled) {
    if (joystickPage) setJoystickPage(false, false);
    setCommandPage(false, false);
    setHomeUiHidden(true);
    lv_obj_clear_flag(recordTimeLabel, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(recordTimeLabel, "录音  00:00");
    lv_obj_align(recordTimeLabel, LV_ALIGN_CENTER, 0, -76);
    for (float& level : waveformLevels) level = 0.0f;
    for (lv_obj_t* bar : waveform) {
      lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
    }
  } else {
    setHomeUiHidden(false);
    lv_obj_add_flag(recordTimeLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_arc_color(usageArc, lv_color_hex(0x00E0D0),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(usageArc, 10, LV_PART_INDICATOR);
    lv_label_set_text(pageLabel, "");
    for (int i = 0; i < 6; ++i) {
      lv_obj_set_style_text_font(agentLabels[i], &lv_font_montserrat_16, 0);
      char label[4];
      snprintf(label, sizeof(label), "%d", i + 1);
      setAgentCaption(i, label);
    }
    for (lv_obj_t* bar : waveform) {
      lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void agentEvent(lv_event_t* event) {
  const int index = static_cast<int>(
      reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) markInteraction();

  if (micRecording) {
    if (code == LV_EVENT_CLICKED && (index == 4 || index == 5)) {
      recordingStopRequested = true;
      recordingSendAfterStop = index == 5;
    }
    return;
  }

  if (code == LV_EVENT_LONG_PRESSED && !commandPage) {
    suppressAgentClick[index] = true;
    detailAgent = index;
    detailUntil = millis() + 5000;
    return;
  }

  if (code != LV_EVENT_CLICKED) return;
  if (suppressAgentClick[index]) {
    suppressAgentClick[index] = false;
    return;
  }

  if (commandPage) {
    if (index == 4) {
      recordingStartRequested = true;
      return;
    }
    sendMomentary(kCommandIds[index]);
    return;
  }

  // The six primary Agent keys are deliberately immediate. Waiting for a
  // possible double-tap made a normal press feel broken on the device.
  sendAgent(index);
}


void setReconnectLayout(bool enabled) {
  for (int i = 0; i < 6; ++i) {
    if (agentButtons[i] == nullptr) continue;
    if (enabled)
      lv_obj_add_flag(agentButtons[i], LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_clear_flag(agentButtons[i], LV_OBJ_FLAG_HIDDEN);
  }
}

void updateCenterPreview(uint8_t stage, uint32_t heldMs) {
  setReconnectLayout(false);
  switch (stage) {
    case 1:
      lv_label_set_text(pageLabel, "快捷操作");
      lv_label_set_text(usageLabel, "6");
      lv_label_set_text(resetLabel, "松开启动");
      lv_obj_set_style_arc_color(usageArc, lv_color_hex(activePrimary),
                                 LV_PART_INDICATOR);
      lv_obj_set_style_arc_width(usageArc, 10, LV_PART_INDICATOR);
      lv_arc_set_value(usageArc, 100);
      break;
    case 2:
      lv_label_set_text(pageLabel, "摇杆");
      lv_label_set_text(usageLabel, "4");
      lv_label_set_text(resetLabel, "松开启动");
      lv_obj_set_style_arc_color(usageArc, lv_color_hex(activePrimary),
                                 LV_PART_INDICATOR);
      lv_obj_set_style_arc_width(usageArc, 10, LV_PART_INDICATOR);
      lv_arc_set_value(usageArc, 100);
      break;
    case 3:
      setReconnectLayout(true);
      lv_label_set_text(pageLabel, "重新连接");
      lv_obj_set_style_text_font(usageLabel, &lv_font_montserrat_48, 0);
      lv_label_set_text(usageLabel, "10");
      lv_label_set_text(resetLabel, "松开重连");
      lv_obj_set_style_arc_color(usageArc, lv_color_hex(kRed),
                                 LV_PART_INDICATOR);
      lv_obj_set_style_arc_width(usageArc, 10, LV_PART_INDICATOR);
      lv_arc_set_value(usageArc, 100);
      break;
    case 4:
      lv_label_set_text(pageLabel, "恢复出厂");
      {
        const int secondsLeft = max(0, 20 - static_cast<int>(heldMs / 1000));
        char countdown[8];
        snprintf(countdown, sizeof(countdown), "%02d", secondsLeft);
        lv_label_set_text(usageLabel, countdown);
      }
      lv_label_set_text(resetLabel, "继续按住");
      lv_obj_set_style_arc_color(usageArc, lv_color_hex(kRed),
                                 LV_PART_INDICATOR);
      lv_obj_set_style_arc_width(usageArc, 10, LV_PART_INDICATOR);
      lv_arc_set_value(usageArc,
                       min(100, static_cast<int>((heldMs - 10000) / 100)));
      break;
    case 5:
      lv_label_set_text(pageLabel, "恢复出厂");
      lv_label_set_text(usageLabel, "00");
      lv_label_set_text(resetLabel, "正在重启");
      lv_obj_set_style_arc_color(usageArc, lv_color_hex(kRed),
                                 LV_PART_INDICATOR);
      lv_arc_set_value(usageArc, 100);
      break;
  }
}

void centerEvent(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) {
    centerReleaseHandled = false;
    markInteraction();
    animateZoom(usageArc, 248, 80);
  }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    animateZoom(usageArc, 256, 180);
  }
  if (micRecording) {
    if (code == LV_EVENT_RELEASED) {
      recordingStopRequested = true;
      recordingSendAfterStop = false;
    }
    return;
  }

  if (code == LV_EVENT_PRESSED) {
    centerPressedAt = millis();
    centerLongPressStage = 0;
    pairResetTriggered = false;
  } else if (code == LV_EVENT_PRESSING && !pairResetTriggered &&
             !commandPage) {
    const uint32_t heldMs = millis() - centerPressedAt;
    if (heldMs >= 800 && centerLongPressStage < 1) {
      centerLongPressStage = 1;
      updateCenterPreview(centerLongPressStage, heldMs);
    }
    if (heldMs >= 5000 && centerLongPressStage < 2) {
      centerLongPressStage = 2;
      updateCenterPreview(centerLongPressStage, heldMs);
    }
    if (heldMs >= 10000 && centerLongPressStage < 3) {
      centerLongPressStage = 3;
      updateCenterPreview(centerLongPressStage, heldMs);
    }
    if (heldMs >= 15000 && centerLongPressStage < 4) {
      centerLongPressStage = 4;
      updateCenterPreview(centerLongPressStage, heldMs);
    }
    if (heldMs >= 20000 && centerLongPressStage < 5) {
      centerLongPressStage = 5;
      pairResetTriggered = true;
      updateCenterPreview(centerLongPressStage, heldMs);
      lv_refr_now(nullptr);
      delay(350);
      if (transport != nullptr) transport->factoryResetAndRestart();
    }
  } else if (code == LV_EVENT_RELEASED && !pairResetTriggered) {
    const uint32_t heldMs = millis() - centerPressedAt;
    // A press inside the shortcut page always returns home.
    if (commandPage) {
      setCommandPage(false, false);
      centerLongPressStage = 0;
      centerReleaseHandled = true;
      return;
    }
    // A press inside the joystick page returns home.
    if (joystickPage) {
      setJoystickPage(false, false);
      centerLongPressStage = 0;
      centerReleaseHandled = true;
      return;
    }

    setReconnectLayout(false);
    if (centerLongPressStage == 0) {
      // A short tap on the home center sends the current Codex input.
      sendMomentary("ACT12");
      centerReleaseHandled = true;
      return;
    }

    switch (centerLongPressStage) {
      case 1:
        setCommandPage(true);
        break;
      case 2:
        ignoreNextNavCenterClick = true;
        setJoystickPage(true);
        break;
      case 3:
      case 4:
        lv_label_set_text(pageLabel, "连接中");
        lv_label_set_text(usageLabel, "");
        lv_label_set_text(resetLabel, "等待中");
        lv_refr_now(nullptr);
        if (transport != nullptr) transport->requestRebond();
        setCommandPage(false, false);
        setJoystickPage(false, false);
        break;
      default:
        setCommandPage(true);
        break;
    }
    centerLongPressStage = 0;
    centerReleaseHandled = true;
  } else if (code == LV_EVENT_PRESS_LOST && !pairResetTriggered) {
    setReconnectLayout(false);
    centerLongPressStage = 0;
    centerReleaseHandled = true;
  } else if (code == LV_EVENT_CLICKED && !centerReleaseHandled &&
             !micRecording && !commandPage && !joystickPage &&
             centerLongPressStage == 0) {
    // Some touch-controller paths emit CLICKED without a reliable RELEASED.
    // Keep the fallback idempotent so a normal tap sends exactly once.
    sendMomentary("ACT12");
    centerReleaseHandled = true;
  }
}

void updateWaveform();
void inputTimer(lv_timer_t*) {
  // Update waveform at 50 FPS for smooth animation during recording
  if (micRecording) updateWaveform();
}

void surfaceEvent(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) markInteraction();
}

void sendJoystickTap(float angle) {
  if (transport == nullptr) return;
  transport->sendJoystick(angle, 1.0f);
  delay(18);
  transport->sendJoystick(angle, 0.0f);
}

void sendEncoderStep(const char* key) {
  if (transport == nullptr) return;
  transport->sendKey(key, 2);
}

void navEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !joystickPage) return;
  if (ignoreNextNavCenterClick) {
    ignoreNextNavCenterClick = false;
    return;
  }
  lv_indev_t* indev = lv_indev_get_act();
  if (indev == nullptr) return;

  lv_point_t point;
  lv_indev_get_point(indev, &point);
  const float dx = point.x - kCenter;
  const float dy = point.y - kCenter;
  const float radius = sqrtf(dx * dx + dy * dy);
  markInteraction();

  if (radius <= 62.0f) {
    setJoystickPage(false);
    return;
  }
  if (radius < 72.0f || radius > 170.0f) return;

  if (fabsf(dx) > fabsf(dy)) {
    // Match the physical Codex dial. Its active mode remains selectable in
    // Codex; this page only emits one forward/backward encoder detent.
    sendEncoderStep(dx > 0 ? "ENC_CC" : "ENC_CW");
  } else if (dy > 0) {
    sendJoystickTap(0.25f);
  } else {
    sendJoystickTap(0.75f);
  }
}

void updateWaveform() {
  const uint32_t elapsed = millis() - recordingStartedAt;
  const float liveLevel = sound == nullptr ? 0.0f : sound->microphoneLevel();
  for (int i = 0; i < kWaveBars - 1; ++i) {
    waveformLevels[i] = waveformLevels[i + 1];
  }
  waveformLevels[kWaveBars - 1] = liveLevel;

  for (int i = 0; i < kWaveBars; ++i) {
    // Always-animating baseline wave so the display is never static
    const float t = elapsed / 1000.0f;
    const float baseWave =
        0.20f + 0.18f * sinf(t * 3.1f + i * 0.48f) +
        0.09f * sinf(t * 5.7f + i * 0.31f);
    const float micShaped =
        powf(constrain(waveformLevels[i], 0.0f, 1.0f), 0.78f);
    const float combined = constrain(baseWave + micShaped * 0.82f, 0.0f, 1.0f);
    const int height = 4 + static_cast<int>(104.0f * combined);
    const int x = kWaveStartX + i * kWaveStepX;
    lv_obj_set_size(waveform[i], 4, height);
    lv_obj_set_pos(waveform[i], x, kWaveCenterY - height / 2);
    lv_obj_set_style_bg_opa(
        waveform[i], static_cast<lv_opa_t>(100 + 155 * combined), 0);
  }

  const uint32_t seconds = elapsed / 1000;
  char timer[20];
  snprintf(timer, sizeof(timer), "录音  %02lu:%02lu",
           static_cast<unsigned long>(seconds / 60),
           static_cast<unsigned long>(seconds % 60));
  lv_label_set_text(recordTimeLabel, timer);
  lv_obj_align(recordTimeLabel, LV_ALIGN_CENTER, 0, -76);
}

void updateAgentVisuals(const CodexMicroState& state) {
  const uint32_t now = millis();

  if (commandPage) {
    bool hasWaiting = false;
    bool hasComplete = false;
    for (const ThreadLight& light : state.threads) {
      const char* status = threadStatus(light);
      hasWaiting = hasWaiting || strcmp(status, "WAITING") == 0;
      hasComplete = hasComplete || strcmp(status, "COMPLETE") == 0;
    }
    if (hasWaiting) {
      lv_label_set_text(pageLabel, "等待处理");
      lv_label_set_text(usageLabel, "2");
      lv_label_set_text(resetLabel, "同意 / 拒绝");
    } else if (hasComplete) {
      lv_label_set_text(pageLabel, "已完成");
      lv_label_set_text(usageLabel, "100%");
      lv_label_set_text(resetLabel, "分支 / 发送");
    } else {
      lv_label_set_text(pageLabel, "快捷操作");
      lv_label_set_text(usageLabel, "6");
      lv_label_set_text(resetLabel, "点击执行");
    }

    for (int i = 0; i < 6; ++i) {
      bool emphasized = false;
      uint32_t commandColor = activePrimary;
      if (hasWaiting && i == 1) {
        emphasized = true;
        commandColor = 0x48EE98;
      } else if (hasWaiting && i == 2) {
        emphasized = true;
        commandColor = kRed;
      } else if (!hasWaiting && hasComplete && i == 3) {
        emphasized = true;
        commandColor = 0x48EE98;
      }
      lv_obj_set_style_border_color(agentButtons[i], lv_color_hex(commandColor),
                                    0);
      lv_obj_set_style_shadow_color(agentButtons[i], lv_color_hex(commandColor),
                                    0);
      lv_obj_set_style_shadow_opa(agentButtons[i],
                                  emphasized ? LV_OPA_70 : LV_OPA_20, 0);
      lv_obj_set_style_transform_zoom(agentButtons[i], emphasized ? 260 : 256,
                                      0);
    }
    return;
  }

  for (int i = 0; i < 6; ++i) {
    const uint32_t targetAccent = scaleColor(state.threads[i].color,
                                             state.threads[i].brightness);
    const bool assigned = state.threads[i].color != 0 &&
                          state.threads[i].brightness > 0.01f;
    const uint32_t targetFill = assigned ? targetAccent : 0xB9C8EC;
    if (!agentAccentInitialized[i]) {
      displayedAgentAccent[i] = targetFill;
      agentAccentInitialized[i] = true;
    } else {
      displayedAgentAccent[i] =
          blendColor(displayedAgentAccent[i], targetFill, 0.18f);
    }
    const uint32_t fill = displayedAgentAccent[i];
    const uint32_t border = 0x1C2944;
    const uint32_t textColor = readableTextColor(fill);
    const uint8_t r = (targetAccent >> 16) & 0xFF;
    const uint8_t g = (targetAccent >> 8) & 0xFF;
    const uint8_t b = targetAccent & 0xFF;
    const bool running = assigned &&
                         (state.threads[i].effect == "breath" ||
                          (b > r * 1.15f && b >= g));
    const lv_opa_t glow = running ? LV_OPA_60
                                  : (assigned ? LV_OPA_30 : LV_OPA_20);

    lv_obj_set_style_bg_color(agentButtons[i], lv_color_hex(fill), 0);
    lv_obj_set_style_bg_grad_dir(agentButtons[i], LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(agentButtons[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(agentButtons[i], lv_color_hex(border), 0);
    lv_obj_set_style_shadow_color(agentButtons[i], lv_color_hex(fill), 0);
    lv_obj_set_style_shadow_opa(agentButtons[i], glow, 0);
    lv_obj_set_style_transform_zoom(agentButtons[i], 256, 0);
    lv_obj_set_style_text_color(agentLabels[i], lv_color_hex(textColor), 0);
    if (micRecording) {
      lv_obj_set_style_border_color(agentButtons[i], lv_color_hex(kDim), 0);
      lv_obj_set_style_shadow_opa(agentButtons[i], LV_OPA_0, 0);
      lv_obj_set_style_transform_zoom(agentButtons[i], 256, 0);
    }
  }
}

}  // namespace

void CodexUi::begin(CodexMicroBle* codex, AudioFeedback* audio) {
  codex_ = codex;
  audio_ = audio;
  transport = codex;
  sound = audio;

  lv_obj_t* screen = lv_scr_act();
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(screen, kBg, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(screen, surfaceEvent, LV_EVENT_ALL, nullptr);

  for (int i = 0; i < 6; ++i) {
    const float angle = (-90.0f + i * 60.0f) * PI / 180.0f;
    const int cx = kCenter + lroundf(kAgentOrbit * cosf(angle));
    const int cy = kCenter + lroundf(kAgentOrbit * sinf(angle));

    lv_obj_t* button = lv_btn_create(screen);
    agentButtons[i] = button;
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, kAgentSize, kAgentSize);
    lv_obj_set_pos(button, cx - kAgentSize / 2, cy - kAgentSize / 2);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xAFC5F2), 0);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x1C2944), 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(0x223457), 0);
    lv_obj_set_style_shadow_width(button, 9, 0);
    lv_obj_set_style_shadow_spread(button, 1, 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_transform_zoom(button, 248, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(button, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_add_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(button, agentEvent, LV_EVENT_ALL,
                        reinterpret_cast<void*>(static_cast<intptr_t>(i)));

    char text[4];
    snprintf(text, sizeof(text), "%d", i + 1);
    lv_obj_t* label = makeLabel(button, text, &lv_font_montserrat_16,
                                lv_color_hex(0x53627A));
    agentLabels[i] = label;
    lv_obj_center(label);
  }

  centerDisk = lv_obj_create(screen);
  lv_obj_remove_style_all(centerDisk);
  lv_obj_set_size(centerDisk, 150, 150);
  lv_obj_center(centerDisk);
  lv_obj_set_style_radius(centerDisk, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(centerDisk, lv_color_hex(0x02050A), 0);
  lv_obj_set_style_bg_opa(centerDisk, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(centerDisk, 2, 0);
  lv_obj_set_style_border_color(centerDisk, lv_color_hex(0x26324A), 0);
  lv_obj_clear_flag(centerDisk,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  usageArc = lv_arc_create(screen);
  lv_obj_remove_style_all(usageArc);
  lv_obj_set_size(usageArc, kUsageSize, kUsageSize);
  lv_obj_center(usageArc);
  lv_arc_set_rotation(usageArc, 270);
  lv_arc_set_bg_angles(usageArc, 0, 360);
  lv_arc_set_range(usageArc, 0, 100);
  lv_arc_set_value(usageArc, 0);
  lv_obj_remove_style(usageArc, nullptr, LV_PART_KNOB);
  lv_obj_set_style_pad_all(usageArc, 0, 0);
  lv_obj_set_style_arc_width(usageArc, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_color(usageArc, lv_color_hex(0x142234), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(usageArc, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(usageArc, false, LV_PART_MAIN);
  lv_obj_set_style_arc_width(usageArc, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(usageArc, lv_color_hex(0x00E0D0),
                             LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(usageArc, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(usageArc, false, LV_PART_INDICATOR);
  lv_obj_clear_flag(usageArc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  const int gapPositions[4][4] = {
      {178, 88, 4, 18}, {254, 178, 18, 4},
      {178, 254, 4, 18}, {88, 178, 18, 4}};
  for (int i = 0; i < 4; ++i) {
    lv_obj_t* divider = lv_obj_create(screen);
    usageDividers[i] = divider;
    lv_obj_remove_style_all(divider);
    lv_obj_set_pos(divider, gapPositions[i][0], gapPositions[i][1]);
    lv_obj_set_size(divider, gapPositions[i][2], gapPositions[i][3]);
    lv_obj_set_style_bg_color(divider, kBg, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_clear_flag(divider,
                      LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  }

  lv_obj_move_foreground(centerDisk);

  for (int i = 0; i < kWaveBars; ++i) {
    lv_obj_t* bar = lv_obj_create(screen);
    waveform[i] = bar;
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 4, 4);
    lv_obj_set_pos(bar, kWaveStartX + i * kWaveStepX,
                   kWaveCenterY - 2);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(i % 2 ? kCyan : kBlue), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(bar, 5, 0);
    lv_obj_set_style_shadow_color(bar, lv_color_hex(kCyan), 0);
    lv_obj_set_style_shadow_opa(bar, LV_OPA_40, 0);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  }

  centerHit = lv_obj_create(screen);
  lv_obj_remove_style_all(centerHit);
  lv_obj_set_size(centerHit, 162, 162);
  lv_obj_center(centerHit);
  lv_obj_set_style_radius(centerHit, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(centerHit, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(centerHit, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_clear_flag(centerHit, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(centerHit, centerEvent, LV_EVENT_ALL, nullptr);

  pageLabel = makeLabel(screen, "", &lv_font_codex_ui_16,
                        lv_color_hex(0xAAB6D0));
  lv_obj_align(pageLabel, LV_ALIGN_CENTER, 0, -45);

  usageLabel = makeLabel(screen, "", &lv_font_montserrat_48,
                         lv_color_white());
  lv_obj_align(usageLabel, LV_ALIGN_CENTER, 0, -3);

  resetLabel = makeLabel(screen, "", &lv_font_codex_ui_16,
                         lv_color_hex(0xAAB6D0));
  lv_obj_align(resetLabel, LV_ALIGN_CENTER, 0, 43);

  recordTimeLabel = makeLabel(screen, "录音  00:00", &lv_font_codex_ui_16,
                              lv_color_hex(kIce));
  lv_obj_align(recordTimeLabel, LV_ALIGN_CENTER, 0, -76);
  lv_obj_add_flag(recordTimeLabel, LV_OBJ_FLAG_HIDDEN);

  detailTitleLabel = makeLabel(screen, "", &lv_font_codex_ui_16,
                               lv_color_hex(kIce));
  lv_obj_set_width(detailTitleLabel, 112);
  lv_label_set_long_mode(detailTitleLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(detailTitleLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(detailTitleLabel, LV_ALIGN_CENTER, 0, -8);
  lv_obj_add_flag(detailTitleLabel, LV_OBJ_FLAG_HIDDEN);

  detailMetaLabel = makeLabel(screen, "", &lv_font_codex_ui_16,
                              lv_color_hex(0x8CB5CA));
  lv_obj_set_width(detailMetaLabel, 112);
  lv_label_set_long_mode(detailMetaLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(detailMetaLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(detailMetaLabel, LV_ALIGN_CENTER, 0, 14);
  lv_obj_add_flag(detailMetaLabel, LV_OBJ_FLAG_HIDDEN);

  connectionDot = lv_obj_create(screen);
  lv_obj_remove_style_all(connectionDot);
  lv_obj_set_size(connectionDot, 7, 7);
  lv_obj_set_pos(connectionDot, 177, 113);
  lv_obj_set_style_radius(connectionDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(connectionDot, lv_color_hex(0xE7A93B), 0);
  lv_obj_set_style_bg_opa(connectionDot, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(connectionDot, 5, 0);
  lv_obj_set_style_shadow_color(connectionDot, lv_color_hex(0x00E0D0), 0);
  lv_obj_set_style_shadow_opa(connectionDot, LV_OPA_50, 0);
  lv_obj_clear_flag(connectionDot,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  batteryLabel = makeLabel(screen, "", &lv_font_montserrat_12,
                           lv_color_hex(0xD6E0F5));
  lv_obj_align(batteryLabel, LV_ALIGN_CENTER, 0, 77);
  lv_obj_set_style_text_align(batteryLabel, LV_TEXT_ALIGN_CENTER, 0);

  batteryIcon = lv_obj_create(screen);
  lv_obj_remove_style_all(batteryIcon);
  lv_obj_set_size(batteryIcon, 32, 14);
  lv_obj_set_pos(batteryIcon, 154, 60);
  lv_obj_set_style_radius(batteryIcon, 3, 0);
  lv_obj_set_style_bg_color(batteryIcon, lv_color_hex(0x061725), 0);
  lv_obj_set_style_bg_opa(batteryIcon, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(batteryIcon, 1, 0);
  lv_obj_set_style_border_color(batteryIcon, lv_color_hex(kDim), 0);
  lv_obj_clear_flag(batteryIcon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  batteryCap = lv_obj_create(screen);
  lv_obj_remove_style_all(batteryCap);
  lv_obj_set_size(batteryCap, 3, 6);
  lv_obj_set_pos(batteryCap, 186, 64);
  lv_obj_set_style_radius(batteryCap, 1, 0);
  lv_obj_set_style_bg_color(batteryCap, lv_color_hex(kDim), 0);
  lv_obj_set_style_bg_opa(batteryCap, LV_OPA_COVER, 0);
  lv_obj_clear_flag(batteryCap, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  batteryFill = lv_obj_create(screen);
  lv_obj_remove_style_all(batteryFill);
  lv_obj_set_size(batteryFill, 28, 10);
  lv_obj_set_pos(batteryFill, 156, 62);
  lv_obj_set_style_radius(batteryFill, 2, 0);
  lv_obj_set_style_bg_color(batteryFill, lv_color_hex(kCyan), 0);
  lv_obj_set_style_bg_opa(batteryFill, LV_OPA_COVER, 0);
  lv_obj_clear_flag(batteryFill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  setObjectHidden(batteryLabel, true);
  setObjectHidden(batteryIcon, true);
  setObjectHidden(batteryCap, true);
  setObjectHidden(batteryFill, true);

  navRing = lv_arc_create(screen);
  lv_obj_remove_style_all(navRing);
  lv_obj_set_size(navRing, 286, 286);
  lv_obj_center(navRing);
  lv_arc_set_bg_angles(navRing, 0, 360);
  lv_obj_set_style_arc_width(navRing, 60, LV_PART_MAIN);
  lv_obj_set_style_arc_color(navRing, lv_color_hex(0x07141D), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(navRing, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(navRing, false, LV_PART_MAIN);
  lv_obj_clear_flag(navRing, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  // Four arc segments centered on TOP(270), RIGHT(0), BOTTOM(90), LEFT(180).
  // LVGL arc: 0=right, angles increase clockwise.
  // Each arc spans 82° with 8° gaps; rotation = center - 41°.
  const int navRotations[4] = {229, 319, 49, 139};
  const char* navTexts[4] = {"^\n新任务", ">\n前进", "v\n侧边栏", "<\n后退"};
  // Label offsets from screen center, placed at ~135px radius (arc band center is 143).
  const int navOffsetX[4] = {0, 118, 0, -118};
  const int navOffsetY[4] = {-118, 0, 118, 0};
  for (int i = 0; i < 4; ++i) {
    lv_obj_t* arc = lv_arc_create(screen);
    navArcs[i] = arc;
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, 286, 286);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, navRotations[i]);
    lv_arc_set_bg_angles(arc, 0, 82);
    lv_obj_set_style_arc_width(arc, 50, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x143C4A), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = makeLabel(screen, navTexts[i], &lv_font_codex_ui_16,
                                lv_color_hex(kIce));
    navLabels[i] = label;
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, navOffsetX[i], navOffsetY[i]);
  }

  navCenter = lv_obj_create(screen);
  lv_obj_remove_style_all(navCenter);
  lv_obj_set_size(navCenter, 88, 88);
  lv_obj_center(navCenter);
  lv_obj_set_style_radius(navCenter, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(navCenter, lv_color_hex(0x031018), 0);
  lv_obj_set_style_bg_opa(navCenter, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(navCenter, 2, 0);
  lv_obj_set_style_border_color(navCenter, lv_color_hex(kCyan), 0);
  lv_obj_set_style_shadow_width(navCenter, 14, 0);
  lv_obj_set_style_shadow_color(navCenter, lv_color_hex(kBlue), 0);
  lv_obj_set_style_shadow_opa(navCenter, LV_OPA_30, 0);
  lv_obj_clear_flag(navCenter, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  navCenterLabel = makeLabel(screen, "退出", &lv_font_codex_ui_16,
                             lv_color_hex(kIce));
  lv_obj_center(navCenterLabel);

  navSurface = lv_obj_create(screen);
  lv_obj_remove_style_all(navSurface);
  lv_obj_set_size(navSurface, 360, 360);
  lv_obj_set_pos(navSurface, 0, 0);
  lv_obj_set_style_bg_opa(navSurface, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(navSurface, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(navSurface, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(navSurface, navEvent, LV_EVENT_CLICKED, nullptr);
  setJoystickUiHidden(true);

  lv_obj_move_foreground(centerHit);
  lv_obj_move_foreground(pageLabel);
  lv_obj_move_foreground(usageLabel);
  lv_obj_move_foreground(resetLabel);
  lv_obj_move_foreground(recordTimeLabel);
  lv_obj_move_foreground(detailTitleLabel);
  lv_obj_move_foreground(detailMetaLabel);
  lv_obj_move_foreground(connectionDot);
  lv_obj_move_foreground(batteryIcon);
  lv_obj_move_foreground(batteryCap);
  lv_obj_move_foreground(batteryFill);
  lv_obj_move_foreground(batteryLabel);

  lv_timer_create(inputTimer, 20, nullptr);
  markInteraction();
}

void CodexUi::setRecording(bool recording) {
  if (micRecording == recording) return;
  micRecording = recording;
  if (recording) recordingStartedAt = millis();
  markInteraction();
  setRecordingVisual(recording);
}

bool CodexUi::takeRecordingStopRequest(bool* sendAfterStop) {
  if (!recordingStopRequested) return false;
  recordingStopRequested = false;
  if (sendAfterStop != nullptr) *sendAfterStop = recordingSendAfterStop;
  recordingSendAfterStop = false;
  return true;
}

bool CodexUi::takeRecordingStartRequest() {
  if (!recordingStartRequested) return false;
  recordingStartRequested = false;
  return true;
}

void CodexUi::wake() { markInteraction(); }

uint32_t CodexUi::lastInteractionAt() const { return uiLastInteractionAt; }

void CodexUi::update(const CodexMicroState& state, int batteryPercent,
                     bool charging) {
  lv_obj_set_style_bg_color(
      connectionDot,
      lv_color_hex(state.bleConnected ? 0x00E0D0 : 0xE7A93B), 0);
  updateBatteryVisual(batteryPercent, charging);

  if (micRecording) {
    lv_obj_set_style_arc_color(usageArc, lv_color_hex(activePrimary),
                               LV_PART_INDICATOR);
    for (int i = 0; i < kWaveBars; ++i) {
      lv_obj_set_style_bg_color(
          waveform[i], lv_color_hex(i % 2 ? activePrimary : activeSecondary), 0);
      lv_obj_set_style_shadow_color(waveform[i], lv_color_hex(activePrimary), 0);
    }
    setObjectHidden(batteryLabel, true);
    setObjectHidden(batteryIcon, true);
    setObjectHidden(batteryCap, true);
    setObjectHidden(batteryFill, true);
    return;
  }

  if (joystickPage) {
    setObjectHidden(batteryLabel, true);
    setObjectHidden(batteryIcon, true);
    setObjectHidden(batteryCap, true);
    setObjectHidden(batteryFill, true);
    return;
  }

  setObjectHidden(batteryLabel, false);
  setObjectHidden(batteryIcon, false);
  setObjectHidden(batteryCap, false);
  setObjectHidden(batteryFill, false);

  updateAgentVisuals(state);

  if (commandPage || centerLongPressStage > 0) {
    setObjectHidden(batteryLabel, true);
    setObjectHidden(batteryIcon, true);
    setObjectHidden(batteryCap, true);
    setObjectHidden(batteryFill, true);
    return;
  }

  if (detailAgent >= 0 && millis() < detailUntil) {
    const ThreadLight& light = state.threads[detailAgent];
    const char* status = threadStatus(light);
    const CompanionAgentMetadata& metadata = state.companionAgents[detailAgent];
    char agent[4];
    snprintf(agent, sizeof(agent), "%d", detailAgent + 1);
    lv_label_set_text(pageLabel, agent);
    lv_obj_add_flag(usageLabel, LV_OBJ_FLAG_HIDDEN);
    setObjectHidden(batteryLabel, true);
    setObjectHidden(batteryIcon, true);
    setObjectHidden(batteryCap, true);
    setObjectHidden(batteryFill, true);
    const String safeTitle = safeTextForDisplay(metadata.title, "");
    const String safeWorkspace = safeTextForDisplay(metadata.workspace, "");
    if (renderedDetailAgent != detailAgent ||
        renderedDetailTitleSource != metadata.title ||
        renderedDetailWorkspaceSource != metadata.workspace) {
      lv_label_set_text(detailTitleLabel, safeTitle.c_str());
      lv_label_set_text(detailMetaLabel, safeWorkspace.c_str());
      renderedDetailAgent = detailAgent;
      renderedDetailTitleSource = metadata.title;
      renderedDetailWorkspaceSource = metadata.workspace;
    }
    if (safeTitle.isEmpty()) {
      lv_obj_add_flag(detailTitleLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(detailTitleLabel, LV_OBJ_FLAG_HIDDEN);
    }
    if (safeWorkspace.isEmpty()) {
      lv_obj_add_flag(detailMetaLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(detailMetaLabel, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_align(detailTitleLabel, LV_ALIGN_CENTER, 0, -8);
    lv_obj_align(detailMetaLabel, LV_ALIGN_CENTER, 0, 14);

    const char* displayStatus = localizedStatus(status);
    char runtime[48];
    if (state.companionEpoch >= metadata.createdAt && metadata.createdAt > 0) {
      uint32_t elapsed = state.companionEpoch - metadata.createdAt;
      if (state.companionUpdatedAt > 0) {
        elapsed += (millis() - state.companionUpdatedAt) / 1000;
      }
      char duration[16];
      formatDuration(duration, sizeof(duration), elapsed);
      snprintf(runtime, sizeof(runtime), "%s  %s", displayStatus, duration);
    } else {
      snprintf(runtime, sizeof(runtime), "%s", displayStatus);
    }
    lv_label_set_text(resetLabel, runtime);
    lv_arc_set_value(usageArc,
                     constrain(static_cast<int>(light.brightness * 100), 0, 100));
    const bool assigned = light.color != 0 && light.brightness > 0.01f;
    const uint32_t accent = scaleColor(light.color, light.brightness);
    lv_obj_set_style_arc_color(usageArc,
                               lv_color_hex(assigned ? accent : 0x18353B),
                               LV_PART_INDICATOR);
    return;
  }
  if (detailAgent >= 0) detailAgent = -1;
  renderedDetailAgent = -1;
  lv_obj_add_flag(detailTitleLabel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(detailMetaLabel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(usageLabel, LV_OBJ_FLAG_HIDDEN);

  lv_label_set_text(pageLabel, "");
  lv_obj_set_style_arc_color(usageArc, lv_color_hex(0x00E0D0),
                             LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(usageArc, 10, LV_PART_INDICATOR);

  if (state.weeklyLeft >= 0) {
    lv_obj_set_style_text_font(usageLabel, &lv_font_montserrat_32, 0);
    lv_obj_clear_flag(usageLabel, LV_OBJ_FLAG_HIDDEN);
    char usage[8];
    snprintf(usage, sizeof(usage), "%d%%", state.weeklyLeft);
    lv_label_set_text(usageLabel, usage);
    lv_arc_set_value(usageArc, state.weeklyLeft);
    lv_obj_set_style_arc_color(usageArc, lv_color_hex(quotaAccent(state.weeklyLeft)),
                               LV_PART_INDICATOR);
    if (state.resetSeconds >= 0) {
      const int days = state.resetSeconds / 86400;
      const int hours = (state.resetSeconds % 86400) / 3600;
      char reset[24];
      snprintf(reset, sizeof(reset), "重置 %d天 %02d时", days, hours);
      lv_label_set_text(resetLabel, reset);
    } else {
      lv_label_set_text(resetLabel, "");
    }
  } else {
    lv_obj_set_style_text_font(usageLabel, &lv_font_codex_ui_16, 0);
    if (state.bleConnected) {
      lv_label_set_text(usageLabel, "未同步额度");
      lv_label_set_text(resetLabel, "等待额度");
    } else {
      lv_label_set_text(usageLabel, "等待连接");
      lv_label_set_text(resetLabel, "");
    }
    lv_arc_set_value(usageArc, 0);
    lv_obj_set_style_arc_color(usageArc, lv_color_hex(kCyan),
                               LV_PART_INDICATOR);
  }
}
