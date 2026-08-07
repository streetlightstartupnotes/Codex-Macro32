// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLECharacteristic.h>
#include <BLEHIDDevice.h>
#include <BLEServer.h>
#include <BLEService.h>

#include <array>

struct ThreadLight {
  uint32_t color = 0;
  float brightness = 0.0f;
  String effect = "off";
  float speed = 0.0f;
};

struct LightingSide {
  uint32_t color = 0;
  float brightness = 0.0f;
  String effect = "off";
  float speed = 0.0f;
};

struct CodexMicroState {
  std::array<ThreadLight, 6> threads;
  LightingSide ambient;
  LightingSide keys;
  bool connected = false;
  bool dirty = true;
  int weeklyLeft = -1;
  int resetSeconds = -1;
  uint32_t usageUpdatedAt = 0;
};

class CodexMicroBle {
 public:
  static constexpr uint16_t kVendorId = 0x303A;
  static constexpr uint16_t kProductId = 0x8360;
  static constexpr uint8_t kReportId = 6;

  void begin();
  void setBattery(uint8_t percentage, bool charging);
  void sendKey(const char* key, uint8_t action, int8_t agent = -1);
  void sendJoystick(float angle, float distance);
  void reopenAdvertising();
  void factoryResetAndRestart();
  bool connected();
  CodexMicroState snapshot();
  const String& deviceId() const { return deviceId_; }
  const String& shortId() const { return shortId_; }

 private:
  class ServerCallbacks;
  class OutputCallbacks;
  class CompanionCallbacks;

  void onConnected(bool connected);
  void onOutput(const uint8_t* data, size_t length);
  void onCompanionWrite(const uint8_t* data, size_t length);
  void handleRpc(const JsonDocument& request);
  void sendResult(JsonVariantConst id, JsonVariantConst result);
  void sendSuccess(JsonVariantConst id);
  void sendJson(const String& json);
  void updateThreadLighting(JsonArrayConst values);
  void updateLightingSide(LightingSide& side, JsonObjectConst value);

  BLEHIDDevice* hid_ = nullptr;
  BLEServer* server_ = nullptr;
  BLECharacteristic* input_ = nullptr;
  BLECharacteristic* output_ = nullptr;
  BLEService* companionService_ = nullptr;
  BLECharacteristic* companion_ = nullptr;
  SemaphoreHandle_t stateMutex_ = nullptr;
  CodexMicroState state_;
  String rpcBuffer_;
  String deviceId_;
  String shortId_;
  uint8_t batteryPercentage_ = 100;
  bool charging_ = false;
};
