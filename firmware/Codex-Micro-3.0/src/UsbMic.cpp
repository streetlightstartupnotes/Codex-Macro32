// SPDX-License-Identifier: MIT

#include "UsbMic.h"

#if defined(CODEX_MACRO32_USB_MIC)

#include <Arduino.h>
#include <USB.h>
#include <USBAudioCard.h>
#include <USBCDC.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "AudioFeedback.h"

#ifndef CODEX_MACRO32_USB_MIC_SYNTHETIC
#define CODEX_MACRO32_USB_MIC_SYNTHETIC 0
#endif

// Arduino-ESP32 retains the string pointer. Keep the replacement static and
// change only the generic AudioControl interface name shown by macOS.
extern "C" uint8_t __real_tinyusb_add_string_descriptor(const char* value);
extern "C" uint8_t __wrap_tinyusb_add_string_descriptor(const char* value) {
  static constexpr char kGenericInterfaceName[] = "TinyUSB UAC1";
  static constexpr char kMicrophoneInterfaceName[] = "Codex Macro32 Mic";
  static constexpr char kGenericCdcName[] = "TinyUSB CDC";
  static constexpr char kMaintenanceInterfaceName[] =
      "Codex Macro32 Maintenance";
  if (value != nullptr && strcmp(value, kGenericInterfaceName) == 0) {
    value = kMicrophoneInterfaceName;
  } else if (value != nullptr && strcmp(value, kGenericCdcName) == 0) {
    value = kMaintenanceInterfaceName;
  }
  return __real_tinyusb_add_string_descriptor(value);
}

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr size_t kSamplesPerBlock = 480;  // 10 ms
constexpr size_t kBytesPerBlock = kSamplesPerBlock * sizeof(int16_t);
constexpr uint32_t kCaptureStackBytes = 4096;
constexpr UBaseType_t kCapturePriority = 4;

USBAudioCard audioCard(kSampleRate, UAC_BPS_16, UAC_SPK_NONE, UAC_MIC_MONO);
// A CDC interface is retained only as a maintenance path. It carries no Codex
// controls or usage data; its DTR/RTS and 1200-baud handling lets esptool ask
// the ESP32-S3 to reboot into the ROM downloader without a BOOT-button cycle.
AudioFeedback* audioSource = nullptr;
TaskHandle_t captureTask = nullptr;
std::atomic<bool> interfaceEnabled{false};
std::atomic<bool> usbUsable{false};
std::atomic<bool> pipelineReady{false};
portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;
codex_usb_mic::Stats pipelineStats = {};

bool streamActive() {
  return pipelineReady.load(std::memory_order_acquire) &&
         interfaceEnabled.load(std::memory_order_acquire) &&
         usbUsable.load(std::memory_order_acquire);
}

void publishTransitionIfChanged(bool previous, bool current) {
  if (previous == current) return;
  portENTER_CRITICAL(&statsMux);
  ++pipelineStats.streamTransitions;
  pipelineStats.streaming = current;
  portEXIT_CRITICAL(&statsMux);
}

void audioEventCallback(void*, esp_event_base_t eventBase, int32_t eventId,
                        void* eventData) {
  if (eventBase != ARDUINO_USB_AUDIO_CARD_EVENTS ||
      eventId != ARDUINO_USB_AUDIO_CARD_INTERFACE_ENABLE_EVENT ||
      eventData == nullptr) {
    return;
  }
  const auto* data =
      static_cast<const arduino_usb_audio_card_event_data_t*>(eventData);
  if (data->interface_enable.interface != UAC_INTERFACE_MIC) return;

  const bool before = streamActive();
  interfaceEnabled.store(data->interface_enable.enable,
                         std::memory_order_release);
  publishTransitionIfChanged(before, streamActive());
}

void usbEventCallback(void*, esp_event_base_t eventBase, int32_t eventId,
                      void*) {
  if (eventBase != ARDUINO_USB_EVENTS) return;
  const bool before = streamActive();
  switch (eventId) {
    case ARDUINO_USB_STARTED_EVENT:
    case ARDUINO_USB_RESUME_EVENT:
      usbUsable.store(true, std::memory_order_release);
      break;
    case ARDUINO_USB_STOPPED_EVENT:
      interfaceEnabled.store(false, std::memory_order_release);
      usbUsable.store(false, std::memory_order_release);
      break;
    case ARDUINO_USB_SUSPEND_EVENT:
      usbUsable.store(false, std::memory_order_release);
      break;
    default:
      return;
  }
  publishTransitionIfChanged(before, streamActive());
}

#if CODEX_MACRO32_USB_MIC_SYNTHETIC
void fillSynthetic(int16_t* samples) {
  static uint32_t phase = 0;
  for (size_t i = 0; i < kSamplesPerBlock; ++i, ++phase) {
    samples[i] = static_cast<int16_t>(
        sinf(2.0f * PI * 1000.0f * phase / kSampleRate) * 12000.0f);
  }
}
#endif

void noteCapture(size_t sampleCount) {
  portENTER_CRITICAL(&statsMux);
  pipelineStats.capturedSamples += sampleCount;
  portEXIT_CRITICAL(&statsMux);
}

void noteWrite(size_t requested, size_t accepted) {
  portENTER_CRITICAL(&statsMux);
  pipelineStats.writtenBytes += accepted;
  if (accepted == 0) {
    ++pipelineStats.zeroWrites;
  } else if (accepted < requested) {
    ++pipelineStats.shortWrites;
  }
  portEXIT_CRITICAL(&statsMux);
}

void captureAudio(void*) {
  int16_t samples[kSamplesPerBlock] = {};
  for (;;) {
    size_t sampleCount = 0;
#if CODEX_MACRO32_USB_MIC_SYNTHETIC
    fillSynthetic(samples);
    sampleCount = kSamplesPerBlock;
    vTaskDelay(pdMS_TO_TICKS(10));
#else
    if (audioSource != nullptr) {
      sampleCount =
          audioSource->readMicrophoneSamples(samples, kSamplesPerBlock);
    }
#endif
    if (sampleCount == 0) {
      vTaskDelay(1);
      continue;
    }
    noteCapture(sampleCount);

    // The codec is captured continuously so the on-device recording waveform
    // remains live even before macOS selects the microphone alternate setting.
    if (!streamActive()) continue;

    const uint8_t* cursor = reinterpret_cast<const uint8_t*>(samples);
    size_t remaining = sampleCount * sizeof(int16_t);
    while (remaining != 0 && streamActive()) {
      const uint16_t request = static_cast<uint16_t>(
          min(remaining, static_cast<size_t>(UINT16_MAX)));
      const uint16_t accepted = audioCard.write(cursor, request);
      noteWrite(request, accepted);
      if (accepted == 0) {
        vTaskDelay(1);
        continue;
      }
      cursor += accepted;
      remaining -= accepted;
    }
  }
}

void stopPipeline() {
  pipelineReady.store(false, std::memory_order_release);
  if (captureTask != nullptr) {
    vTaskDelete(captureTask);
    captureTask = nullptr;
  }
  audioCard.end();
  portENTER_CRITICAL(&statsMux);
  pipelineStats.ready = false;
  pipelineStats.streaming = false;
  portEXIT_CRITICAL(&statsMux);
}

}  // namespace

namespace codex_usb_mic {

bool begin(AudioFeedback* audio) {
  if (pipelineReady.load(std::memory_order_acquire)) return true;
#if !CODEX_MACRO32_USB_MIC_SYNTHETIC
  if (audio == nullptr || !audio->ready()) {
    Serial.println("USB mic unavailable: board microphone not ready");
    return false;
  }
#endif
  audioSource = audio;

  audioCard.onEvent(ARDUINO_USB_AUDIO_CARD_INTERFACE_ENABLE_EVENT,
                    audioEventCallback);
  USB.onEvent(usbEventCallback);
  if (!audioCard.begin()) {
    Serial.println("USB mic audio class start failed");
    return false;
  }
  if (xTaskCreatePinnedToCore(captureAudio, "usb_mic_capture",
                              kCaptureStackBytes, nullptr, kCapturePriority,
                              &captureTask, 0) != pdPASS) {
    Serial.println("USB mic capture task start failed");
    stopPipeline();
    return false;
  }

  char serialNumber[17];
  snprintf(serialNumber, sizeof(serialNumber), "%016llX",
           static_cast<unsigned long long>(ESP.getEfuseMac()));
  USB.VID(0x303A);
  USB.PID(0x8361);
  USB.productName("Codex Macro32 Mic");
  USB.manufacturerName("streetlightstartupnotes");
  USB.serialNumber(serialNumber);
  USB.firmwareVersion(0x0300);
  USB.usbVersion(0x0200);
  USB.usbClass(0);
  USB.usbSubClass(0);
  USB.usbProtocol(0);
  USB.usbPower(500);
  USB.webUSB(false);

  // Construct after Arduino has initialized the USB event loop, but before
  // USB.begin() freezes the composite descriptor. This avoids cross-file
  // global-constructor ordering differences between toolchain versions.
  static USBCDC maintenanceSerial(0);
  maintenanceSerial.begin(115200);
  maintenanceSerial.enableReboot(true);

  // Publish ready before USB.begin(): once the native port switches from
  // Serial/JTAG to TinyUSB UAC, the final log line may no longer be visible.
  pipelineReady.store(true, std::memory_order_release);
  portENTER_CRITICAL(&statsMux);
  pipelineStats.ready = true;
  portEXIT_CRITICAL(&statsMux);
  Serial.printf(
      "USB_MIC_START rate=48000 bits=16 channels=1 speaker=0 synthetic=%u\n",
      CODEX_MACRO32_USB_MIC_SYNTHETIC ? 1U : 0U);
  if (!USB.begin()) {
    Serial.println("USB mic TinyUSB start failed");
    stopPipeline();
    return false;
  }
  return true;
}

bool ready() { return pipelineReady.load(std::memory_order_acquire); }

bool streaming() { return streamActive(); }

Stats snapshotStats() {
  portENTER_CRITICAL(&statsMux);
  const Stats result = pipelineStats;
  portEXIT_CRITICAL(&statsMux);
  return result;
}

}  // namespace codex_usb_mic

#else

namespace codex_usb_mic {
bool begin(AudioFeedback*) { return false; }
bool ready() { return false; }
bool streaming() { return false; }
Stats snapshotStats() { return {}; }
}  // namespace codex_usb_mic

#endif
