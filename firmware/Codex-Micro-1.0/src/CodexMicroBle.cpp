// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#include "CodexMicroBle.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEUtils.h>
#include <esp_gap_ble_api.h>
#include <nvs_flash.h>

#include <vector>

namespace {

constexpr char kDeviceName[] = "Codex Micro";
constexpr char kManufacturer[] = "Work Louder";
constexpr char kFirmwareVersion[] = "0.2.0-waveshare-1.85b";
constexpr char kCompanionServiceUuid[] = "df2b7c00-76b6-4b6c-a8c7-c653e4342010";
constexpr char kCompanionCharacteristicUuid[] = "df2b7c01-76b6-4b6c-a8c7-c653e4342010";
constexpr size_t kPayloadSize = 61;
constexpr size_t kReportBodySize = 63;

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

  const uint64_t chipId = ESP.getEfuseMac();
  char serial[13];
  snprintf(serial, sizeof(serial), "%012llX",
           static_cast<unsigned long long>(chipId));
  deviceId_ = serial;
  shortId_ = deviceId_.substring(deviceId_.length() - 4);

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

void CodexMicroBle::onCompanionWrite(const uint8_t* data, size_t length) {
  if (data == nullptr || length == 0 || length > 256) return;
  StaticJsonDocument<256> document;
  const DeserializationError error = deserializeJson(document, data, length);
  if (error) {
    Serial.printf("Companion JSON error: %s\n", error.c_str());
    return;
  }
  const int weeklyLeft = document["weekly_left"] | -1;
  const int resetSeconds = document["reset_seconds"] | -1;
  if (weeklyLeft < 0 || weeklyLeft > 100) return;

  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  state_.weeklyLeft = weeklyLeft;
  state_.resetSeconds = max(-1, resetSeconds);
  state_.usageUpdatedAt = millis();
  state_.dirty = true;
  xSemaphoreGive(stateMutex_);

  if (companion_ != nullptr) {
    companion_->setValue("{\"ok\":true}");
    companion_->notify();
  }
  Serial.printf("Usage weekly_left=%d reset_seconds=%d\n", weeklyLeft,
                resetSeconds);
}

void CodexMicroBle::setBattery(uint8_t percentage, bool charging) {
  batteryPercentage_ = constrain(percentage, 0, 100);
  charging_ = charging;
  if (hid_ != nullptr && connected()) {
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
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  const bool result = state_.connected;
  xSemaphoreGive(stateMutex_);
  return result;
}

CodexMicroState CodexMicroBle::snapshot() {
  CodexMicroState copy;
  if (stateMutex_ == nullptr) {
    return copy;
  }
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  copy = state_;
  state_.dirty = false;
  xSemaphoreGive(stateMutex_);
  return copy;
}

void CodexMicroBle::onConnected(bool connected) {
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  state_.connected = connected;
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
  if (input_ == nullptr) {
    return;
  }
  if (!connected()) {
    return;
  }

  String framed = json;
  framed += '\n';
  size_t offset = 0;
  while (offset < framed.length()) {
    const size_t chunk = min<size_t>(kPayloadSize, framed.length() - offset);
    uint8_t report[kReportBodySize] = {};
    report[0] = 2;
    report[1] = chunk;
    memcpy(report + 2, framed.c_str() + offset, chunk);
    input_->setValue(report, sizeof(report));
    input_->notify();
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
