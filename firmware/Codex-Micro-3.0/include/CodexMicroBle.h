// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLECharacteristic.h>
#include <BLEHIDDevice.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <array>
#include <atomic>

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

// Metadata supplied by the optional macOS companion. It is deliberately kept
// separate from vendor-HID lighting state so the transport can evolve without
// changing the desktop lighting protocol.
struct CompanionAgentMetadata {
  String title;
  String workspace;
  String cwd;
  char status = 'u';
  uint32_t createdAt = 0;
  uint32_t updatedAt = 0;
};

struct CodexMicroState {
  std::array<ThreadLight, 6> threads;
  std::array<CompanionAgentMetadata, 6> companionAgents;
  LightingSide ambient;
  LightingSide keys;
  bool connected = false;
  bool bleConnected = false;
  bool bleAuthenticated = false;
  bool hidReady = false;
  bool companionReady = false;
  bool dirty = true;
  int weeklyLeft = -1;
  int resetSeconds = -1;
  uint32_t usageUpdatedAt = 0;
  uint32_t companionUpdatedAt = 0;
  uint32_t companionEpoch = 0;
  uint16_t companionSyncInterval = 300;
  uint8_t companionAgentCount = 0;
  bool companionSoundEnabled = true;
};

class CodexMicroBle {
 public:
  static constexpr uint16_t kVendorId = 0x303A;
  static constexpr uint16_t kProductId = 0x8360;
  static constexpr uint8_t kReportId = 6;

  void begin();
  void poll();
  void setBattery(uint8_t percentage, bool charging);
  void sendKey(const char* key, uint8_t action, int8_t agent = -1);
  void sendJoystick(float angle, float distance);
  void reopenAdvertising();
  void requestRebond();
  void factoryResetAndRestart();
  bool connected();
  CodexMicroState snapshot();
  const String& deviceId() const { return deviceId_; }
  const String& shortId() const { return shortId_; }

 private:
  class ServerCallbacks;
  class SecurityCallbacks;
  class OutputCallbacks;
  class CompanionCallbacks;

  enum class PendingWriteKind : uint8_t {
    HidOutput = 1,
    Companion = 2,
  };

  static constexpr size_t kMaxPendingWriteBytes = 256;
  static constexpr size_t kPendingWriteDepth = 16;

  struct PendingWrite {
    PendingWriteKind kind = PendingWriteKind::HidOutput;
    uint16_t length = 0;
    uint8_t data[kMaxPendingWriteBytes] = {};
  };

  void onConnected(bool connected);
  void onAuthenticationComplete(bool success);
  void enqueueWrite(PendingWriteKind kind, const uint8_t* data, size_t length);
  void processOutput(const uint8_t* data, size_t length);
  void processCompanionWrite(const uint8_t* data, size_t length);
  void restoreUsage();
  bool persistUsage(int weeklyLeft, int resetSeconds);
  void restoreCompanionConfig();
  bool persistCompanionConfig(uint16_t syncInterval, bool soundEnabled);
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
  StaticQueue_t pendingWriteQueueStorage_ = {};
  uint8_t pendingWriteQueueBuffer_[kPendingWriteDepth * sizeof(PendingWrite)] = {};
  QueueHandle_t pendingWriteQueue_ = nullptr;
  std::atomic<uint32_t> droppedWritePackets_{0};
  SemaphoreHandle_t stateMutex_ = nullptr;
  CodexMicroState state_;
  String rpcBuffer_;
  String deviceId_;
  String shortId_;
  uint8_t resetReason_ = 0;
  uint8_t batteryPercentage_ = 100;
  bool charging_ = false;
};
