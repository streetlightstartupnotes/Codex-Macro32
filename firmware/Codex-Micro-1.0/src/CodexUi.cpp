// Project publication and maintenance: 路灯同学创业笔记
// X: https://x.com/LDstartupnotes
// Xiaohongshu: https://www.xiaohongshu.com/user/profile/63fd97c1000000001400d0ea
// https://github.com/streetlightstartupnotes

#include "CodexUi.h"

#include <Arduino.h>
#include <lvgl.h>
#include <cmath>

#include "AudioFeedback.h"

namespace {

constexpr int kCenter = 180;
constexpr int kOrbit = 110;
constexpr int kAgentSize = 72;
constexpr int kUsageSize = 148;
const lv_color_t kBg = LV_COLOR_MAKE(0x00, 0x00, 0x00);

CodexMicroBle* transport = nullptr;
AudioFeedback* sound = nullptr;
lv_obj_t* agentButtons[6] = {};
lv_obj_t* agentLabels[6] = {};
lv_obj_t* usageArc = nullptr;
lv_obj_t* centerDisk = nullptr;
lv_obj_t* centerHit = nullptr;
lv_obj_t* connectionDot = nullptr;
lv_obj_t* usageLabel = nullptr;
lv_obj_t* resetLabel = nullptr;
lv_obj_t* pageLabel = nullptr;
bool commandPage = false;
bool pairResetTriggered = false;
bool bleConnected = false;
uint32_t centerPressedAt = 0;
bool centerTapPending = false;
uint32_t centerTapAt = 0;

const char* kCommandNames[6] = {"FAST", "OK", "NO", "FORK", "MIC", "SEND"};
const char* kCommandIds[6] = {"ACT06", "ACT07", "ACT08", "ACT09", "ACT10", "ACT12"};

uint32_t scaleColor(uint32_t raw, float brightness) {
  if (raw == 0 || brightness <= 0.01f) return 0xB9C8EC;
  const float factor = constrain(brightness, 0.28f, 1.0f);
  const uint8_t r = static_cast<uint8_t>(((raw >> 16) & 0xFF) * factor);
  const uint8_t g = static_cast<uint8_t>(((raw >> 8) & 0xFF) * factor);
  const uint8_t b = static_cast<uint8_t>((raw & 0xFF) * factor);
  return (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | b;
}

void sendMomentary(const char* id, int agent = -1) {
  if (transport == nullptr) return;
  transport->sendKey(id, 1, agent);
  delay(18);
  transport->sendKey(id, 0, agent);
  if (sound != nullptr) sound->tap();
}

void agentEvent(lv_event_t* event) {
  const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (commandPage) {
    sendMomentary(kCommandIds[index]);
  } else {
    char id[5];
    snprintf(id, sizeof(id), "AG%02d", index);
    sendMomentary(id, index);
  }
}

void toggleCommandPage() {
  commandPage = !commandPage;
  lv_label_set_text(pageLabel, commandPage ? "COMMAND MODE" : "WEEKLY LEFT");
  for (int i = 0; i < 6; ++i) {
    if (commandPage) {
      lv_label_set_text(agentLabels[i], kCommandNames[i]);
      lv_obj_set_style_bg_color(agentButtons[i], lv_color_hex(0x172033), 0);
      lv_obj_set_style_border_color(agentButtons[i], lv_color_hex(0x56E7D5), 0);
    } else {
      char label[4];
      snprintf(label, sizeof(label), "A%d", i + 1);
      lv_label_set_text(agentLabels[i], label);
    }
  }
  if (commandPage) {
    lv_label_set_text(usageLabel, "6");
    lv_label_set_text(resetLabel, "COMMAND KEYS");
    lv_arc_set_value(usageArc, 100);
  }
  if (sound != nullptr) sound->tap();
}

void centerEvent(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) {
    centerPressedAt = millis();
    pairResetTriggered = false;
  } else if (code == LV_EVENT_PRESSING && !pairResetTriggered) {
    const uint32_t heldMs = millis() - centerPressedAt;
    if (heldMs >= 3000) {
      const int secondsLeft = max(0, 20 - static_cast<int>(heldMs / 1000));
      char countdown[8];
      snprintf(countdown, sizeof(countdown), "%d", secondsLeft);
      lv_label_set_text(pageLabel, "FACTORY RESET");
      lv_label_set_text(usageLabel, countdown);
      lv_label_set_text(resetLabel, "KEEP HOLDING");
      lv_arc_set_value(usageArc, min(100, static_cast<int>(heldMs / 200)));
    }
    if (heldMs >= 20000) {
      pairResetTriggered = true;
      lv_label_set_text(pageLabel, "RESET COMPLETE");
      lv_label_set_text(usageLabel, "OK");
      lv_label_set_text(resetLabel, "RESTARTING");
      lv_refr_now(nullptr);
      if (sound != nullptr) sound->completionChime();
      delay(350);
      transport->factoryResetAndRestart();
    }
  } else if (code == LV_EVENT_RELEASED && !pairResetTriggered) {
    const uint32_t heldMs = millis() - centerPressedAt;
    if (heldMs >= 550) {
      toggleCommandPage();
    } else if (commandPage) {
      sendMomentary("ENC");
    } else if (centerTapPending && millis() - centerTapAt <= 350) {
      centerTapPending = false;
      if (bleConnected) {
        // A connected double tap is still only one Send.
        sendMomentary("ACT12");
      } else {
        lv_label_set_text(resetLabel, "CONNECTING");
        lv_refr_now(nullptr);
        transport->reopenAdvertising();
        if (sound != nullptr) sound->tap();
      }
    } else {
      centerTapPending = true;
      centerTapAt = millis();
    }
  }
}

void centerTapTimer(lv_timer_t*) {
  if (centerTapPending && millis() - centerTapAt > 350) {
    centerTapPending = false;
    if (bleConnected) sendMomentary("ACT12");
  }
}

void gestureEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_GESTURE || transport == nullptr) return;
  lv_dir_t direction = lv_indev_get_gesture_dir(lv_indev_get_act());
  float angle = 0.0f;
  if (direction == LV_DIR_RIGHT) angle = 0.0f;
  else if (direction == LV_DIR_BOTTOM) angle = 0.25f;
  else if (direction == LV_DIR_LEFT) angle = 0.50f;
  else if (direction == LV_DIR_TOP) angle = 0.75f;
  else return;
  transport->sendJoystick(angle, 1.0f);
  delay(18);
  transport->sendJoystick(angle, 0.0f);
  if (sound != nullptr) sound->tap();
}

lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, const lv_font_t* font,
                    lv_color_t color) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  return label;
}

}  // namespace

void CodexUi::begin(CodexMicroBle* codex, AudioFeedback* audio) {
  codex_ = codex;
  audio_ = audio;
  transport = codex;
  sound = audio;

  lv_obj_t* screen = lv_scr_act();
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_style_bg_color(screen, kBg, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(screen, gestureEvent, LV_EVENT_GESTURE, nullptr);

  for (int i = 0; i < 6; ++i) {
    const float angle = (-90.0f + i * 60.0f) * PI / 180.0f;
    const int cx = kCenter + lroundf(kOrbit * cosf(angle));
    const int cy = kCenter + lroundf(kOrbit * sinf(angle));
    lv_obj_t* button = lv_btn_create(screen);
    agentButtons[i] = button;
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, kAgentSize, kAgentSize);
    lv_obj_set_pos(button, cx - kAgentSize / 2, cy - kAgentSize / 2);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xAFC5F2), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x1C2944), 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(0x223457), 0);
    lv_obj_set_style_shadow_width(button, 9, 0);
    lv_obj_set_style_shadow_spread(button, 1, 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_add_event_cb(button, agentEvent, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(i)));

    char text[4];
    snprintf(text, sizeof(text), "A%d", i + 1);
    lv_obj_t* label = makeLabel(button, text, &lv_font_montserrat_12,
                                lv_color_hex(0x53627A));
    agentLabels[i] = label;
    lv_obj_center(label);
  }

  centerDisk = lv_obj_create(screen);
  lv_obj_remove_style_all(centerDisk);
  lv_obj_set_size(centerDisk, 126, 126);
  lv_obj_center(centerDisk);
  lv_obj_set_style_radius(centerDisk, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(centerDisk, lv_color_hex(0x02050A), 0);
  lv_obj_set_style_bg_opa(centerDisk, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(centerDisk, 2, 0);
  lv_obj_set_style_border_color(centerDisk, lv_color_hex(0x26324A), 0);
  lv_obj_clear_flag(centerDisk, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  usageArc = lv_arc_create(screen);
  lv_obj_remove_style_all(usageArc);
  lv_obj_set_size(usageArc, kUsageSize, kUsageSize);
  lv_obj_center(usageArc);
  lv_arc_set_rotation(usageArc, 270);
  lv_arc_set_bg_angles(usageArc, 0, 360);
  lv_arc_set_range(usageArc, 0, 100);
  lv_arc_set_value(usageArc, 0);
  lv_obj_remove_style(usageArc, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(usageArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_pad_all(usageArc, 0, 0);
  lv_obj_set_style_arc_width(usageArc, 9, LV_PART_MAIN);
  lv_obj_set_style_arc_color(usageArc, lv_color_hex(0x142234), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(usageArc, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(usageArc, false, LV_PART_MAIN);
  lv_obj_set_style_arc_width(usageArc, 9, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(usageArc, lv_color_hex(0x00E0D0), LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(usageArc, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(usageArc, false, LV_PART_INDICATOR);
  lv_obj_clear_flag(usageArc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  // The reference hardware breaks the cyan ring at the four cardinal points.
  const int gapPositions[4][4] = {
      {178, 104, 4, 13}, {243, 178, 13, 4},
      {178, 243, 4, 13}, {104, 178, 13, 4}};
  for (const auto& gap : gapPositions) {
    lv_obj_t* divider = lv_obj_create(screen);
    lv_obj_remove_style_all(divider);
    lv_obj_set_pos(divider, gap[0], gap[1]);
    lv_obj_set_size(divider, gap[2], gap[3]);
    lv_obj_set_style_bg_color(divider, kBg, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  }

  lv_obj_move_foreground(centerDisk);

  centerHit = lv_obj_create(screen);
  lv_obj_remove_style_all(centerHit);
  lv_obj_set_size(centerHit, 136, 136);
  lv_obj_center(centerHit);
  lv_obj_set_style_radius(centerHit, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(centerHit, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(centerHit, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(centerHit, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(centerHit, centerEvent, LV_EVENT_ALL, nullptr);

  pageLabel = makeLabel(screen, "WEEKLY LEFT", &lv_font_montserrat_12,
                        lv_color_hex(0xAAB6D0));
  lv_obj_set_style_text_letter_space(pageLabel, 1, 0);
  lv_obj_align(pageLabel, LV_ALIGN_CENTER, 0, -33);

  usageLabel = makeLabel(screen, "--%", &lv_font_montserrat_32,
                         lv_color_hex(0xF3F6FF));
  lv_obj_align(usageLabel, LV_ALIGN_CENTER, 0, 0);

  resetLabel = makeLabel(screen, "PAIR TO SYNC", &lv_font_montserrat_12,
                         lv_color_hex(0xAAB6D4));
  lv_obj_set_style_text_letter_space(resetLabel, 1, 0);
  lv_obj_align(resetLabel, LV_ALIGN_CENTER, 0, 34);

  connectionDot = lv_obj_create(screen);
  lv_obj_remove_style_all(connectionDot);
  lv_obj_set_size(connectionDot, 7, 7);
  lv_obj_set_pos(connectionDot, 177, 118);
  lv_obj_set_style_radius(connectionDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(connectionDot, lv_color_hex(0xE7A93B), 0);
  lv_obj_set_style_bg_opa(connectionDot, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(connectionDot, 5, 0);
  lv_obj_set_style_shadow_color(connectionDot, lv_color_hex(0x00E0D0), 0);
  lv_obj_set_style_shadow_opa(connectionDot, LV_OPA_50, 0);

  lv_obj_move_foreground(centerHit);
  lv_obj_move_foreground(pageLabel);
  lv_obj_move_foreground(usageLabel);
  lv_obj_move_foreground(resetLabel);
  lv_obj_move_foreground(connectionDot);

  lv_timer_create(centerTapTimer, 20, nullptr);
}

void CodexUi::update(const CodexMicroState& state, int batteryPercent,
                     bool charging) {
  bleConnected = state.connected;
  lv_obj_set_style_bg_color(connectionDot,
                            lv_color_hex(state.connected ? 0x00E0D0 : 0xE7A93B), 0);

  if (commandPage) {
    // Command-page labels are managed by the long-press handler.
  } else if (state.weeklyLeft >= 0) {
    char usage[8];
    snprintf(usage, sizeof(usage), "%d%%", state.weeklyLeft);
    lv_label_set_text(usageLabel, usage);
    lv_arc_set_value(usageArc, state.weeklyLeft);
    if (state.resetSeconds >= 0) {
      const int days = state.resetSeconds / 86400;
      const int hours = (state.resetSeconds % 86400) / 3600;
      char reset[24];
      snprintf(reset, sizeof(reset), "RESET %dD %02dH", days, hours);
      lv_label_set_text(resetLabel, reset);
    }
  } else {
    lv_label_set_text(usageLabel, "--%");
    lv_arc_set_value(usageArc, 0);
    if (state.connected) {
      lv_label_set_text(resetLabel, "SYNCING USAGE");
    } else {
      char pairText[20];
      snprintf(pairText, sizeof(pairText), "PAIR  %s",
               transport != nullptr ? transport->shortId().c_str() : "----");
      lv_label_set_text(resetLabel, pairText);
    }
  }

  if (!commandPage) {
    for (int i = 0; i < 6; ++i) {
      const uint32_t color = scaleColor(state.threads[i].color,
                                        state.threads[i].brightness);
      lv_obj_set_style_bg_color(agentButtons[i], lv_color_hex(color), 0);
      lv_obj_set_style_border_color(agentButtons[i], lv_color_hex(0x1C2944), 0);
      lv_obj_set_style_shadow_color(agentButtons[i], lv_color_hex(color), 0);
      lv_obj_set_style_shadow_opa(agentButtons[i],
                                  state.threads[i].effect == "breath" ? LV_OPA_60
                                                                      : LV_OPA_30,
                                  0);
      const uint8_t r = (color >> 16) & 0xFF;
      const uint8_t g = (color >> 8) & 0xFF;
      const uint8_t b = color & 0xFF;
      lv_obj_set_style_text_color(agentLabels[i],
                                  (r + g + b > 390) ? lv_color_hex(0x172033)
                                                    : lv_color_white(),
                                  0);
    }
  }
}
