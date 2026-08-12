// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#include "CodexMicroBle.h"

#include "CodexWifiOta.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEUtils.h>
#include <Preferences.h>
#include <esp_gap_ble_api.h>
#include <nvs_flash.h>

#include <vector>

namespace {

CodexWifiOta wifiOta;
volatile bool bleTransportConnected = false;

constexpr char kDeviceName[] = "Codex Micro";
constexpr char kManufacturer[] = "Work Louder";
constexpr char kFirmwareVersion[] = "1.1.0-waveshare-1.85b";
constexpr char kCompanionServiceUuid[] = "df2b7c00-76b6-4b6c-a8c7-c653e4342010";
constexpr char kCompanionCharacteristicUuid[] = "df2b7c01-76b6-4b6c-a8c7-c653e4342010";
constexpr size_t kPayloadSize = 61;
constexpr size_t kReportBodySize = 63;
constexpr char kUsagePreferencesNamespace[] = "codex_usage";
constexpr char kUsageSnapshotKey[] = "snapshot";
constexpr char kCompanionConfigKey[] = "config_v2";
constexpr uint32_t kUsageSnapshotMagic = 0x434D5531;  // "CMU1"
constexpr uint32_t kCompanionConfigMagic = 0x434D4332;  // "CMC2"

struct PersistedUsage {
  uint32_t magic;
  int32_t weeklyLeft;
  int32_t resetSeconds;
};

struct PersistedCompanionConfig {
  uint32_t magic;
  uint16_t syncInterval;
  uint8_t flags;
  uint8_t reserved;
};

static_assert(sizeof(PersistedUsage) == 12,
              "Persisted usage snapshot layout changed");
static_assert(sizeof(PersistedCompanionConfig) == 8,
              "Persisted companion config layout changed");

bool validUsage(int weeklyLeft, int resetSeconds) {
  return weeklyLeft >= 0 && weeklyLeft <= 100 && resetSeconds >= -1;
}

bool validCompanionConfig(uint16_t syncInterval) {
  return syncInterval >= 15 && syncInterval <= 3600;
}

constexpr uint16_t swapBytes(uint16_t value) {
  return static_cast<uint16_t>((value << 8) | (value >> 8));
}

// One vendor-defined input/output report. HIDAPI adds/removes Report ID 6,
// while the BLE characteristics carry the remaining 63-byte report body.
const uint8_t kReportMap[] = {
    0x06, 0x00, 0xFF,        // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,              // Usage (1)
    0xA1, 0x01,              // Collection (Application)
    0x85, 0x06,              // Report ID (6)
    0x15, 0x00,              // Logical Minimum (0)
    0x26, 0xFF, 0x00,        // Logical Maximum (255)
    0x75, 0x08,              // Report Size (8)
    0x95, 0x3F,              // Report Count (63)
    0x09, 0x01,              // Usage (1)
    0x81, 0x02,              // Input (Data, Variable, Absolute)
    0x95, 0x3F,              // Report Count (63)
    0x09, 0x02,              // Usage (2)
    0x91, 0x02,              // Output (Data, Variable, Absolute)
    0xC0                     // End Collection
};

class SecurityCallbacks final : public BLESecurityCallbacks {
 public:
  bool onSecurityRequest() override { return true; }
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t) override {}
  bool onConfirmPIN(uint32_t) override { return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override {
    Serial.printf("BLE pairing %s\n", result.success ? "complete" : "failed");
  }
};

}  // namespace

class CodexMicroBle::ServerCallbacks final : public BLEServerCallbacks {
 public:
  explicit ServerCallbacks(CodexMicroBle& owner) : owner_(owner) {}

  void onConnect(BLEServer* server) override {
    owner_.onConnected(true);
    // Keep advertising so the local usage bridge can share the connection
    // with the operating-system HID client.
    server->startAdvertising();
  }

  void onDisconnect(BLEServer* server) override {
    owner_.onConnected(server->getConnectedCount() > 0);
    server->startAdvertising();
  }

 private:
  CodexMicroBle& owner_;
};

class CodexMicroBle::OutputCallbacks final : public BLECharacteristicCallbacks {
 public:
  explicit OutputCallbacks(CodexMicroBle& owner) : owner_(owner) {}

  void onWrite(BLECharacteristic* characteristic) override {
    const String value = characteristic->getValue();
    owner_.onOutput(reinterpret_cast<const uint8_t*>(value.c_str()), value.length());
  }

 private:
  CodexMicroBle& owner_;
};

class CodexMicroBle::CompanionCallbacks final : public BLECharacteristicCallbacks {
 public:
  explicit CompanionCallbacks(CodexMicroBle& owner) : owner_(owner) {}

  void onWrite(BLECharacteristic* characteristic) override {
    const String value = characteristic->getValue();
    owner_.onCompanionWrite(reinterpret_cast<const uint8_t*>(value.c_str()),
                            value.length());
  }

 private:
  CodexMicroBle& owner_;
};

void CodexMicroBle::begin() {
  stateMutex_ = xSemaphoreCreateMutex();
  restoreUsage();
  restoreCompanionConfig();

  const uint64_t chipId = ESP.getEfuseMac();
  char serial[13];
  snprintf(serial, sizeof(serial), "%012llX",
           static_cast<unsigned long long>(chipId));
  deviceId_ = serial;
  shortId_ = deviceId_.substring(deviceId_.length() - 4);

  wifiOta.begin(shortId_);

  BLEDevice::init(kDeviceName);
  BLEDevice::setSecurityCallbacks(new SecurityCallbacks());

  auto* security = new BLESecurity();
  security->setCapability(ESP_IO_CAP_NONE);
  security->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  security->setKeySize(16);
  security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  server_ = BLEDevice::createServer();
  server_->setCallbacks(new ServerCallbacks(*this));

  companionService_ = server_->createService(kCompanionServiceUuid);
  companion_ = companionService_->createCharacteristic(
      kCompanionCharacteristicUuid,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_NOTIFY);
  companion_->setCallbacks(new CompanionCallbacks(*this));
  companion_->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED |
                                   ESP_GATT_PERM_WRITE_ENCRYPTED);
  companion_->addDescriptor(new BLE2902());
  StaticJsonDocument<128> identity;
  identity["ready"] = true;
  identity["device_id"] = deviceId_;
  identity["short_id"] = shortId_;
  String identityJson;
  serializeJson(identity, identityJson);
  companion_->setValue(identityJson.c_str());
  companionService_->start();

  hid_ = new BLEHIDDevice(server_);
  hid_->manufacturer()->setValue(kManufacturer);
  // Low release bits mark the transport as wireless in the desktop bridge.
  // Arduino-ESP32 2.x serializes these fields big-endian, while the BLE PnP
  // characteristic is little-endian. Pre-swap so macOS enumerates 303A:8360.
  hid_->pnp(0x02, swapBytes(kVendorId), swapBytes(kProductId), swapBytes(0x0101));
  hid_->hidInfo(0x00, 0x01);
  hid_->reportMap(const_cast<uint8_t*>(kReportMap), sizeof(kReportMap));

  input_ = hid_->inputReport(kReportId);
  output_ = hid_->outputReport(kReportId);
  output_->setCallbacks(new OutputCallbacks(*this));
  hid_->startServices();
  hid_->setBatteryLevel(batteryPercentage_);

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->setAppearance(GENERIC_HID);
  advertising->addServiceUUID(hid_->hidService()->getUUID());
  advertising->addServiceUUID(kCompanionServiceUuid);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.printf(
      "BLE vendor HID ready VID=%04X PID=%04X usage=FF00 report=%u id=%s\n",
      kVendorId, kProductId, kReportId, deviceId_.c_str());
}

void CodexMicroBle::factoryResetAndRestart() {
  Serial.println("Factory reset: erasing NVS and BLE bonds");
  BLEDevice::stopAdvertising();
  if (server_ != nullptr) {
    const auto peers = server_->getPeerDevices(false);
    for (const auto& peer : peers) {
      server_->disconnect(peer.first);
    }
  }
  delay(250);
  nvs_flash_erase();
  delay(300);
  ESP.restart();
}

void CodexMicroBle::reopenAdvertising() {
  Serial.println("BLE reconnect requested from touch UI");
  BLEDevice::startAdvertising();
}

void CodexMicroBle::requestRebond() {
  Serial.println("Re-pair: disconnect peers, clear bonds and restart advertising");
  BLEDevice::stopAdvertising();
  if (server_ != nullptr) {
    const auto peers = server_->getPeerDevices(false);
    for (const auto& peer : peers) {
      server_->disconnect(peer.first);
    }
  }
  delay(200);

  int dev_num = esp_ble_get_bond_device_num();
  if (dev_num > 0) {
    std::vector<esp_ble_bond_dev_t> dev_list(dev_num);
    esp_ble_get_bond_device_list(&dev_num, dev_list.data());
    for (int i = 0; i < dev_num; ++i) {
      esp_ble_remove_bond_device(dev_list[i].bd_addr);
    }
  }
  esp_ble_gap_clear_whitelist();
  delay(200);
  BLEDevice::startAdvertising();
  Serial.println("Re-pair: advertising restarted");
}

void CodexMicroBle::onCompanionWrite(const uint8_t* data, size_t length) {
  if (data == nullptr || length == 0 || length > 256) return;
  StaticJsonDocument<512> document;
  const DeserializationError error = deserializeJson(document, data, length);
  if (error) {
    Serial.printf("Companion JSON error: %s\n", error.c_str());
    return;
  }

  bool accepted = false;
  bool usageAccepted = false;
  JsonVariantConst weeklyValue = document["weekly_left"];
  JsonVariantConst resetValue = document["reset_seconds"];
  if (weeklyValue.is<int>() && resetValue.is<int>()) {
    const int weeklyLeft = weeklyValue.as<int>();
    const int resetSeconds = resetValue.as<int>();
    if (validUsage(weeklyLeft, resetSeconds)) {
      xSemaphoreTake(stateMutex_, portMAX_DELAY);
      state_.weeklyLeft = weeklyLeft;
      state_.resetSeconds = resetSeconds;
      state_.usageUpdatedAt = millis();
      state_.companionUpdatedAt = millis();
      state_.dirty = true;
      xSemaphoreGive(stateMutex_);
      persistUsage(weeklyLeft, resetSeconds);
      accepted = true;
      usageAccepted = true;
      Serial.printf("Usage weekly_left=%d reset_seconds=%d\n", weeklyLeft,
                    resetSeconds);
    }
  }

  JsonArrayConst config = document["c"].as<JsonArrayConst>();
  if (!config.isNull() && config.size() >= 4 && config[1].is<int>()) {
    const int interval = config[1].as<int>();
    if (interval >= 15 && interval <= 3600) {
      const bool soundEnabled = config[3].as<int>() != 0;
      xSemaphoreTake(stateMutex_, portMAX_DELAY);
      const bool changed =
          state_.companionSyncInterval != static_cast<uint16_t>(interval) ||
          state_.companionSoundEnabled != soundEnabled;
      state_.companionSyncInterval = static_cast<uint16_t>(interval);
      state_.companionSoundEnabled = soundEnabled;
      state_.companionUpdatedAt = millis();
      state_.dirty = true;
      xSemaphoreGive(stateMutex_);
      if (changed) {
        persistCompanionConfig(static_cast<uint16_t>(interval), soundEnabled);
      }
      accepted = true;
    }
  }

  JsonVariantConst countValue = document["n"];
  if (countValue.is<int>()) {
    const int count = countValue.as<int>();
    if (count >= 0 && count <= 6) {
      xSemaphoreTake(stateMutex_, portMAX_DELAY);
      state_.companionAgentCount = static_cast<uint8_t>(count);
      for (size_t index = count; index < state_.companionAgents.size(); ++index) {
        state_.companionAgents[index] = CompanionAgentMetadata{};
      }
      state_.companionUpdatedAt = millis();
      state_.dirty = true;
      xSemaphoreGive(stateMutex_);
      accepted = true;
    }
  }

  JsonVariantConst epochValue = document["ts"];
  if (epochValue.is<uint32_t>()) {
    const uint32_t epoch = epochValue.as<uint32_t>();
    if (epoch >= 1700000000UL) {
      xSemaphoreTake(stateMutex_, portMAX_DELAY);
      state_.companionEpoch = epoch;
      state_.companionUpdatedAt = millis();
      state_.dirty = true;
      xSemaphoreGive(stateMutex_);
      accepted = true;
    }
  }

  JsonArrayConst agent = document["a"].as<JsonArrayConst>();
  if (!agent.isNull() && agent.size() >= 7 && agent[0].is<int>() &&
      agent[1].is<const char*>() && agent[2].is<const char*>() &&
      agent[3].is<const char*>() && agent[4].is<const char*>() &&
      agent[5].is<uint32_t>() && agent[6].is<uint32_t>()) {
    const int index = agent[0].as<int>();
    const char* status = agent[4].as<const char*>();
    const bool validStatus = status != nullptr && status[0] != '\0' &&
                             status[1] == '\0' && strchr("rwieu", status[0]);
    if (index >= 0 && index < 6 && validStatus) {
      xSemaphoreTake(stateMutex_, portMAX_DELAY);
      CompanionAgentMetadata& target = state_.companionAgents[index];
      target.title = agent[1].as<const char*>();
      target.workspace = agent[2].as<const char*>();
      target.cwd = agent[3].as<const char*>();
      target.status = status[0];
      target.createdAt = agent[5].as<uint32_t>();
      target.updatedAt = agent[6].as<uint32_t>();
      const uint8_t receivedCount = static_cast<uint8_t>(index + 1);
      if (receivedCount > state_.companionAgentCount) {
        state_.companionAgentCount = receivedCount;
      }
      state_.companionUpdatedAt = millis();
      state_.dirty = true;
      xSemaphoreGive(stateMutex_);
      accepted = true;
      Serial.printf("Companion agent %d status=%c title=%s\n", index + 1,
                    status[0], target.title.c_str());
    }
  }

  if (!accepted) return;

  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  state_.companionReady = true;
  state_.dirty = true;
  xSemaphoreGive(stateMutex_);

  if (companion_ != nullptr) {
    companion_->setValue("{\"ok\":true,\"v\":11}");
    companion_->notify();
  }
  if (!usageAccepted && document["v"].as<int>() == 11) {
    Serial.println("Companion V1.1 metadata/config accepted");
  }
}

void CodexMicroBle::restoreUsage() {
  Preferences preferences;
  if (!preferences.begin(kUsagePreferencesNamespace, true)) {
    Serial.println("Usage NVS unavailable; starting without cached usage");
    return;
  }

  PersistedUsage persisted = {};
  const size_t storedLength = preferences.getBytesLength(kUsageSnapshotKey);
  const size_t readLength =
      storedLength == sizeof(persisted)
          ? preferences.getBytes(kUsageSnapshotKey, &persisted,
                                 sizeof(persisted))
          : 0;
  preferences.end();

  if (readLength != sizeof(persisted) ||
      persisted.magic != kUsageSnapshotMagic ||
      !validUsage(persisted.weeklyLeft, persisted.resetSeconds)) {
    if (storedLength != 0) {
      Serial.println("Ignoring invalid cached usage snapshot");
    }
    return;
  }

  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  state_.weeklyLeft = persisted.weeklyLeft;
  state_.resetSeconds = persisted.resetSeconds;
  state_.usageUpdatedAt = 0;
  state_.dirty = true;
  xSemaphoreGive(stateMutex_);
  Serial.printf("Restored usage weekly_left=%d reset_seconds=%d\n",
                persisted.weeklyLeft, persisted.resetSeconds);
}

bool CodexMicroBle::persistUsage(int weeklyLeft, int resetSeconds) {
  if (!validUsage(weeklyLeft, resetSeconds)) return false;

  Preferences preferences;
  if (!preferences.begin(kUsagePreferencesNamespace, false)) {
    Serial.println("Usage NVS unavailable; snapshot not saved");
    return false;
  }

  const PersistedUsage persisted = {
      kUsageSnapshotMagic,
      static_cast<int32_t>(weeklyLeft),
      static_cast<int32_t>(resetSeconds),
  };
  const bool saved =
      preferences.putBytes(kUsageSnapshotKey, &persisted, sizeof(persisted)) ==
      sizeof(persisted);
  preferences.end();
  if (!saved) {
    Serial.println("Failed to save usage snapshot to NVS");
  }
  return saved;
}

void CodexMicroBle::restoreCompanionConfig() {
  Preferences preferences;
  if (!preferences.begin(kUsagePreferencesNamespace, true)) {
    Serial.println("Config NVS unavailable; using default settings");
    return;
  }

  PersistedCompanionConfig persisted = {};
  const size_t storedLength = preferences.getBytesLength(kCompanionConfigKey);
  const size_t readLength =
      storedLength == sizeof(persisted)
          ? preferences.getBytes(kCompanionConfigKey, &persisted,
                                 sizeof(persisted))
          : 0;
  preferences.end();

  if (readLength != sizeof(persisted) ||
      persisted.magic != kCompanionConfigMagic ||
      !validCompanionConfig(persisted.syncInterval)) {
    if (storedLength != 0) {
      Serial.println("Ignoring invalid cached companion settings");
    }
    return;
  }

  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  state_.companionSoundEnabled = (persisted.flags & 0x01) != 0;
  state_.companionSyncInterval = persisted.syncInterval;
  state_.dirty = true;
  xSemaphoreGive(stateMutex_);
  Serial.printf("Restored settings interval=%u sound=%u\n",
                state_.companionSyncInterval,
                state_.companionSoundEnabled);
}

bool CodexMicroBle::persistCompanionConfig(uint16_t syncInterval,
                                           bool soundEnabled) {
  if (!validCompanionConfig(syncInterval)) return false;

  Preferences preferences;
  if (!preferences.begin(kUsagePreferencesNamespace, false)) {
    Serial.println("Config NVS unavailable; settings not saved");
    return false;
  }

  const PersistedCompanionConfig persisted = {
      kCompanionConfigMagic,
      syncInterval,
      static_cast<uint8_t>(soundEnabled ? 0x01 : 0x00),
      0,
  };
  const bool saved = preferences.putBytes(kCompanionConfigKey, &persisted,
                                          sizeof(persisted)) ==
                     sizeof(persisted);
  preferences.end();
  if (!saved) {
    Serial.println("Failed to save companion settings to NVS");
  }
  return saved;
}

void CodexMicroBle::setBattery(uint8_t percentage, bool charging) {
  batteryPercentage_ = constrain(percentage, 0, 100);
  charging_ = charging;
  if (hid_ != nullptr && bleTransportConnected) {
    hid_->setBatteryLevel(batteryPercentage_);
  }
}

void CodexMicroBle::sendKey(const char* key, uint8_t action, int8_t agent) {
  StaticJsonDocument<192> message;
  message["method"] = "v.oai.hid";
  JsonObject params = message.createNestedObject("params");
  params["k"] = key;
  params["act"] = action;
  if (agent >= 0) {
    params["ag"] = agent;
  }

  String json;
  serializeJson(message, json);
  sendJson(json);
  Serial.printf("HID key=%s action=%u\n", key, action);
}

void CodexMicroBle::sendJoystick(float angle, float distance) {
  StaticJsonDocument<160> message;
  message["method"] = "v.oai.rad";
  JsonObject params = message.createNestedObject("params");
  params["a"] = angle;
  params["d"] = distance;

  String json;
  serializeJson(message, json);
  sendJson(json);
}

bool CodexMicroBle::connected() {
  if (stateMutex_ == nullptr) {
    return false;
  }
  const bool transportConnected = bleTransportConnected;
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  if (state_.connected != transportConnected) {
    state_.connected = transportConnected;
    state_.dirty = true;
  }
  const bool result = transportConnected;
  xSemaphoreGive(stateMutex_);
  return result;
}

CodexMicroState CodexMicroBle::snapshot() {
  CodexMicroState copy;
  if (stateMutex_ == nullptr) {
    return copy;
  }
  const bool transportConnected = bleTransportConnected;
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  if (state_.connected != transportConnected) {
    state_.connected = transportConnected;
    state_.dirty = true;
  }
  copy = state_;
  state_.dirty = false;
  xSemaphoreGive(stateMutex_);
  return copy;
}

void CodexMicroBle::onConnected(bool connected) {
  bleTransportConnected = connected;
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  const bool wasBleConnected = state_.bleConnected;
  state_.bleConnected = connected;
  state_.connected = connected;
  if (!connected || !wasBleConnected) {
    state_.companionReady = false;
  }
  state_.dirty = true;
  xSemaphoreGive(stateMutex_);
  rpcBuffer_.clear();
  Serial.printf("BLE host %s\n", connected ? "connected" : "disconnected");
}

void CodexMicroBle::onOutput(const uint8_t* data, size_t length) {
  if (data == nullptr || length < 2) {
    return;
  }

  // HOGP normally strips the report ID. Accept an included ID as well so the
  // transport remains compatible with hosts that forward the raw report.
  size_t offset = (length >= 3 && data[0] == kReportId) ? 1 : 0;
  if (length < offset + 2 || data[offset] != 2) {
    return;
  }

  const size_t payloadLength = min<size_t>(data[offset + 1], kPayloadSize);
  if (length < offset + 2 + payloadLength) {
    return;
  }
  const char* payload = reinterpret_cast<const char*>(data + offset + 2);
  constexpr char kTopLevelPrefix[] = "{\"method\"";
  const bool startsTopLevel =
      payloadLength >= sizeof(kTopLevelPrefix) - 1 &&
      memcmp(payload, kTopLevelPrefix, sizeof(kTopLevelPrefix) - 1) == 0;
  if (startsTopLevel && !rpcBuffer_.isEmpty()) {
    // A new top-level object means a previous fragmented write was dropped.
    // Resynchronize immediately instead of poisoning the next request.
    rpcBuffer_.clear();
  }
  if (rpcBuffer_.isEmpty()) {
    size_t jsonStart = 0;
    while (jsonStart < payloadLength && payload[jsonStart] != '{') {
      ++jsonStart;
    }
    if (jsonStart == payloadLength) {
      return;
    }
    rpcBuffer_.concat(payload + jsonStart, payloadLength - jsonStart);
  } else {
    rpcBuffer_.concat(payload, payloadLength);
  }

  DynamicJsonDocument request(4096);
  const DeserializationError error = deserializeJson(request, rpcBuffer_);
  if (error == DeserializationError::IncompleteInput) {
    return;
  }
  if (error) {
    Serial.printf("RPC parse error: %s\n", error.c_str());
    rpcBuffer_.clear();
    return;
  }

  handleRpc(request);
  rpcBuffer_.clear();
}

void CodexMicroBle::handleRpc(const JsonDocument& request) {
  const char* method = request["method"] | "";
  JsonVariantConst id = request["id"];
  JsonVariantConst params = request["params"];
  Serial.printf("RPC method=%s\n", method);

  if (strcmp(method, "sys.version") == 0) {
    StaticJsonDocument<128> resultDoc;
    resultDoc["version"] = kFirmwareVersion;
    sendResult(id, resultDoc.as<JsonVariantConst>());
    return;
  }

  if (strcmp(method, "device.status") == 0) {
    StaticJsonDocument<256> resultDoc;
    resultDoc["version"] = kFirmwareVersion;
    resultDoc["profile_index"] = 0;
    resultDoc["layer_index"] = 1;
    resultDoc["battery"] = batteryPercentage_;
    resultDoc["is_charging"] = charging_;
    resultDoc["device_id"] = deviceId_;
    sendResult(id, resultDoc.as<JsonVariantConst>());
    return;
  }

  if (strcmp(method, "v.oai.thstatus") == 0 && params.is<JsonArrayConst>()) {
    updateThreadLighting(params.as<JsonArrayConst>());
    sendSuccess(id);
    return;
  }

  if (strcmp(method, "v.oai.rgbcfg") == 0 && params.is<JsonObjectConst>()) {
    xSemaphoreTake(stateMutex_, portMAX_DELAY);
    JsonObjectConst config = params.as<JsonObjectConst>();
    updateLightingSide(state_.ambient, config["ambient"].as<JsonObjectConst>());
    updateLightingSide(state_.keys, config["keys"].as<JsonObjectConst>());
    state_.dirty = true;
    xSemaphoreGive(stateMutex_);
    sendSuccess(id);
    return;
  }

  if (strcmp(method, "lights.preview") == 0 || strcmp(method, "host.focused_app") == 0) {
    sendSuccess(id);
    return;
  }

  StaticJsonDocument<192> response;
  response["id"] = id;
  JsonObject error = response.createNestedObject("error");
  error["code"] = -32601;
  error["message"] = "Method not found";
  String json;
  serializeJson(response, json);
  sendJson(json);
}

void CodexMicroBle::sendResult(JsonVariantConst id, JsonVariantConst result) {
  DynamicJsonDocument response(512);
  response["id"] = id;
  response["result"] = result;
  String json;
  serializeJson(response, json);
  sendJson(json);
}

void CodexMicroBle::sendSuccess(JsonVariantConst id) {
  StaticJsonDocument<96> resultDoc;
  resultDoc["ok"] = true;
  sendResult(id, resultDoc.as<JsonVariantConst>());
}

void CodexMicroBle::sendJson(const String& json) {
  const bool sendBle = input_ != nullptr && bleTransportConnected;
  if (!sendBle) return;

  String framed = json;
  framed += '\n';
  size_t offset = 0;
  while (offset < framed.length()) {
    const size_t chunk = min<size_t>(kPayloadSize, framed.length() - offset);
    uint8_t report[kReportBodySize] = {};
    report[0] = 2;
    report[1] = chunk;
    memcpy(report + 2, framed.c_str() + offset, chunk);
    if (sendBle) {
      input_->setValue(report, sizeof(report));
      input_->notify();
    }
    offset += chunk;
    delay(4);
  }
}

void CodexMicroBle::updateThreadLighting(JsonArrayConst values) {
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  for (JsonObjectConst value : values) {
    const int id = value["id"] | -1;
    if (id < 0 || id >= static_cast<int>(state_.threads.size())) {
      continue;
    }
    ThreadLight& light = state_.threads[id];
    light.color = value["c"] | light.color;
    light.brightness = value["b"] | light.brightness;
    light.effect = value["e"] | light.effect;
    light.speed = value["s"] | light.speed;
  }
  state_.dirty = true;
  xSemaphoreGive(stateMutex_);
}

void CodexMicroBle::updateLightingSide(LightingSide& side, JsonObjectConst value) {
  if (value.isNull()) {
    return;
  }
  side.color = value["c"] | side.color;
  side.brightness = value["b"] | side.brightness;
  side.effect = value["e"] | side.effect;
  side.speed = value["s"] | side.speed;
}
