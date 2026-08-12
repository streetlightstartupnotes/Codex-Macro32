/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

// Initializes the Waveshare 1.85B ES7210 dual-microphone ADC as a 48 kHz,
// 16-bit, standard-I2S slave. Wire must already be initialized.
bool es7210_begin(uint32_t sampleRateHz);
