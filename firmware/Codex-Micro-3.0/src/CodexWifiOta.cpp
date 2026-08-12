// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#include "CodexWifiOta.h"

#include "CodexV11Config.h"

#ifndef CODEX_WIFI_OTA_ENABLED
#define CODEX_WIFI_OTA_ENABLED 0
#endif

#if CODEX_WIFI_OTA_ENABLED

#include <ArduinoOTA.h>
#include <WiFi.h>

namespace {

constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr uint32_t kReconnectDelayMs = 60000;
constexpr size_t kOtaTaskStackSize = 8192;

bool validConfiguration() {
  const size_t ssidLength = strlen(CODEX_WIFI_SSID);
  const size_t wifiPasswordLength = strlen(CODEX_WIFI_PASSWORD);
  const size_t otaPasswordLength = strlen(CODEX_OTA_PASSWORD);
  return ssidLength >= 1 && ssidLength <= 32 && wifiPasswordLength >= 8 &&
         wifiPasswordLength <= 63 && otaPasswordLength >= 8;
}

}  // namespace

bool CodexWifiOta::begin(const String& deviceSuffix) {
  if (task_ != nullptr) return enabled_;
  if (!validConfiguration()) {
    Serial.println(
        "Wi-Fi OTA disabled: configure SSID, Wi-Fi password, and OTA password");
    return false;
  }

  hostname_ = "codex-micro-";
  hostname_ += deviceSuffix;
  hostname_.toLowerCase();

  // Do not write network credentials to NVS. This module never receives or
  // stores Codex account credentials, tokens, or API keys.
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);

  const BaseType_t created = xTaskCreate(
      taskEntry, "codex_wifi_ota", kOtaTaskStackSize, this, 1, &task_);
  enabled_ = created == pdPASS;
  if (!enabled_) {
    task_ = nullptr;
    Serial.println("Wi-Fi OTA disabled: worker task creation failed");
  }
  return enabled_;
}

void CodexWifiOta::taskEntry(void* context) {
  static_cast<CodexWifiOta*>(context)->run();
}

void CodexWifiOta::run() {
  bool otaStarted = false;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      connected_ = false;
      if (otaStarted) {
        ArduinoOTA.end();
        otaStarted = false;
      }

      WiFi.disconnect(false, false);
      WiFi.begin(CODEX_WIFI_SSID, CODEX_WIFI_PASSWORD);
      const uint32_t startedAt = millis();
      while (WiFi.status() != WL_CONNECTED &&
             millis() - startedAt < kConnectTimeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(250));
      }

      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi OTA waiting: connection failed; retrying later");
        vTaskDelay(pdMS_TO_TICKS(kReconnectDelayMs));
        continue;
      }

      connected_ = true;
      Serial.printf("Wi-Fi OTA network ready at %s\n",
                    WiFi.localIP().toString().c_str());
    }

    if (!otaStarted) {
      ArduinoOTA.setHostname(hostname_.c_str());
      ArduinoOTA.setPassword(CODEX_OTA_PASSWORD);
      ArduinoOTA.setRebootOnSuccess(true);
      ArduinoOTA.onStart([]() { Serial.println("OTA update started"); });
      ArduinoOTA.onEnd([]() { Serial.println("OTA update complete; rebooting"); });
      ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA update failed: error=%u\n",
                      static_cast<unsigned>(error));
      });
      ArduinoOTA.begin();
      otaStarted = true;
      Serial.printf("Password-protected OTA ready: %s.local\n",
                    hostname_.c_str());
    }

    ArduinoOTA.handle();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

#else

bool CodexWifiOta::begin(const String&) {
  enabled_ = false;
  connected_ = false;
  return false;
}

void CodexWifiOta::taskEntry(void*) {}
void CodexWifiOta::run() {}

#endif
