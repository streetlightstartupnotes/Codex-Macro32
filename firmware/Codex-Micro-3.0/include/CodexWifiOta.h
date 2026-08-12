// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#pragma once

#include <Arduino.h>

class CodexWifiOta {
 public:
  // Returns false when the feature is not compiled in, credentials are
  // absent/invalid, or the worker task cannot be created.
  bool begin(const String& deviceSuffix);
  bool enabled() const { return enabled_; }
  bool connected() const { return connected_; }

 private:
  static void taskEntry(void* context);
  void run();

  String hostname_;
  TaskHandle_t task_ = nullptr;
  volatile bool enabled_ = false;
  volatile bool connected_ = false;
};
