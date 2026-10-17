/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
// https://github.com/espressif/esp-idf/blob/v5.4.2/examples/peripherals/rmt/ir_nec_transceiver
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <driver/gpio.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Task-context API: callers serialize these operations. */
esp_err_t ir_helper_init(gpio_num_t pin_tx);
/* Original NEC frame; waits at most 250 ms, retaining its payload on timeout. */
esp_err_t ir_helper_send(uint8_t addr, uint8_t cmd);
/* Copy alternating mark/space microseconds, starting with mark, then send
   asynchronously at 38 kHz / 50% duty for the AC protocols. Legacy NEC keeps
   38 kHz / 33%. Zeros are skipped without changing input parity.
   Up to 1024 RMT symbols after splitting durations longer than 32767 us.
   An active transmission is never overwritten (ESP_ERR_TIMEOUT while busy). */
esp_err_t ir_helper_send_pulses(const uint16_t *durations, size_t count);
/* ESP_OK means idle/completed, ESP_ERR_TIMEOUT means still sending. */
esp_err_t ir_helper_poll(void);
/* Stop safely and recreate the channel; failed cleanup can be retried. */
esp_err_t ir_helper_cancel(void);

#ifdef __cplusplus
}
#endif
