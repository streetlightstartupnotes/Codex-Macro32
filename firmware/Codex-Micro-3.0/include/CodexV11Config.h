// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#pragma once

// Local credentials are intentionally absent from the repository. Copy
// CodexV11Secrets.example.h to CodexV11Secrets.h and edit the copy to opt in.
#if __has_include("CodexV11Secrets.h")
#include "CodexV11Secrets.h"
#endif

#ifndef CODEX_WIFI_SSID
#define CODEX_WIFI_SSID ""
#endif

#ifndef CODEX_WIFI_PASSWORD
#define CODEX_WIFI_PASSWORD ""
#endif

#ifndef CODEX_OTA_PASSWORD
#define CODEX_OTA_PASSWORD ""
#endif
