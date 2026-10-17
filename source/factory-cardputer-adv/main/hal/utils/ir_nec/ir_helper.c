/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ir_helper.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_attr.h"
#include <stdbool.h>
#include <stdatomic.h>

#define IR_RESOLUTION_HZ 1000000
#define IR_MAX_SYMBOLS 1024
#define IR_MAX_DURATION 32767
#define IR_NEC_TIMEOUT_MS 250

static rmt_channel_handle_t tx_channel;
static rmt_encoder_handle_t copy_encoder;
static gpio_num_t tx_pin = GPIO_NUM_NC;
static bool channel_enabled;
static bool transmission_active;
static unsigned carrier_duty_percent;
_Static_assert(ATOMIC_BOOL_LOCK_FREE == 2, "IR completion flag must be ISR-safe and lock-free");
static atomic_bool transmission_done;
/* RMT encodes asynchronously, so a caller's stack/vector is never its payload. */
static rmt_symbol_word_t symbols[IR_MAX_SYMBOLS];

static bool IRAM_ATTR on_transmission_done(rmt_channel_handle_t channel,
                                           const rmt_tx_done_event_data_t *event, void *context)
{
    (void)channel;
    (void)event;
    (void)context;
    atomic_store_explicit(&transmission_done, true, memory_order_release);
    return false;  /* No task was woken by this callback. */
}

static esp_err_t release_resources(void)
{
    esp_err_t result;
    if (tx_channel && channel_enabled) {
        result = rmt_disable(tx_channel);
        if (result != ESP_OK) return result;  /* Still owns the static payload. */
        channel_enabled = false;
        transmission_active = false;
        atomic_store_explicit(&transmission_done, false, memory_order_release);
    }
    /* Delete the channel before its encoder: a failed channel deletion must not
       leave it holding an encoder that has already been freed. */
    if (tx_channel) {
        result = rmt_del_channel(tx_channel);
        if (result != ESP_OK) return result;
        tx_channel = NULL;
    }
    if (copy_encoder) {
        result = rmt_del_encoder(copy_encoder);
        if (result != ESP_OK) return result;
        copy_encoder = NULL;
    }
    tx_pin = GPIO_NUM_NC;
    transmission_active = false;
    carrier_duty_percent = 0;
    atomic_store_explicit(&transmission_done, false, memory_order_release);
    return ESP_OK;
}

esp_err_t ir_helper_init(gpio_num_t pin_tx)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin_tx)) return ESP_ERR_INVALID_ARG;
    if (tx_channel && copy_encoder && channel_enabled && tx_pin == pin_tx) return ESP_OK;
    if (transmission_active) return ESP_ERR_INVALID_STATE;
    esp_err_t result = release_resources();
    if (result != ESP_OK) return result;

    const rmt_tx_channel_config_t channel_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 1,
        .gpio_num = pin_tx,
    };
    result = rmt_new_tx_channel(&channel_config, &tx_channel);
    if (result != ESP_OK) goto failure;
    const rmt_carrier_config_t carrier_config = {
        .duty_cycle = 0.33f,
        .frequency_hz = 38000,
    };
    result = rmt_apply_carrier(tx_channel, &carrier_config);
    if (result != ESP_OK) goto failure;
    const rmt_copy_encoder_config_t encoder_config = {};
    result = rmt_new_copy_encoder(&encoder_config, &copy_encoder);
    if (result != ESP_OK) goto failure;
    const rmt_tx_event_callbacks_t callbacks = {.on_trans_done = on_transmission_done};
    result = rmt_tx_register_event_callbacks(tx_channel, &callbacks, NULL);
    if (result != ESP_OK) goto failure;
    result = rmt_enable(tx_channel);
    if (result != ESP_OK) goto failure;
    channel_enabled = true;
    carrier_duty_percent = 33;
    tx_pin = pin_tx;
    return ESP_OK;

failure:
    /* Keep any handle whose cleanup itself fails, allowing a later init to
       retry cleanup instead of leaking or reusing an incomplete channel. */
    (void)release_resources();
    return result;
}

esp_err_t ir_helper_poll(void)
{
    if (!transmission_active) return ESP_OK;
    /* A zero-timeout driver wait logs an ERROR for ordinary in-progress work.
       Only reap after TX_DONE has put this transaction in its completed queue. */
    if (!atomic_load_explicit(&transmission_done, memory_order_acquire)) return ESP_ERR_TIMEOUT;
    const esp_err_t result = rmt_tx_wait_all_done(tx_channel, 0);
    if (result == ESP_OK) {
        transmission_active = false;
        atomic_store_explicit(&transmission_done, false, memory_order_release);
    }
    return result;
}

static esp_err_t send_pulses(const uint16_t *durations, size_t count, unsigned duty_percent)
{
    if (!durations || !count) return ESP_ERR_INVALID_ARG;
    if (!tx_channel || !copy_encoder || !channel_enabled) return ESP_ERR_INVALID_STATE;
    esp_err_t result = ir_helper_poll();
    if (result != ESP_OK) return result;

    /* Validate before modifying storage. Zero-length segments are skipped;
       parity still refers to the original mark/space sequence. Long segments
       are split without changing their electrical level. */
    size_t half_count = 0;
    for (size_t i = 0; i < count; ++i) {
        half_count += (durations[i] + (uint32_t)IR_MAX_DURATION - 1) / IR_MAX_DURATION;
        if (half_count > IR_MAX_SYMBOLS * 2) return ESP_ERR_INVALID_SIZE;
    }
    if (!half_count) return ESP_ERR_INVALID_ARG;
    if (carrier_duty_percent != duty_percent) {
        const rmt_carrier_config_t carrier = {
            .duty_cycle = duty_percent / 100.0f,
            .frequency_hz = 38000,
        };
        result = rmt_apply_carrier(tx_channel, &carrier);
        if (result != ESP_OK) return result;
        carrier_duty_percent = duty_percent;
    }
    const size_t symbol_count = (half_count + 1) / 2;
    size_t half = 0;
    for (size_t i = 0; i < count; ++i) {
        uint32_t remaining = durations[i];
        while (remaining) {
            const uint16_t duration = remaining > IR_MAX_DURATION ? IR_MAX_DURATION : (uint16_t)remaining;
            const uint32_t level = (i & 1) == 0;
            rmt_symbol_word_t *symbol = &symbols[half / 2];
            if ((half & 1) == 0) {
                symbol->val = 0;
                symbol->duration0 = duration;
                symbol->level0 = level;
            } else {
                symbol->duration1 = duration;
                symbol->level1 = level;
            }
            remaining -= duration;
            ++half;
        }
    }
    /* An unmatched final half is the sole zero-duration terminal pad. */
    const rmt_transmit_config_t config = {
        .loop_count = 0,
        .flags = {.eot_level = 0, .queue_nonblocking = 1},
    };
    /* Clear before submission: even an ISR completing before transmit returns
       must remain observable by the first poll. */
    atomic_store_explicit(&transmission_done, false, memory_order_release);
    result = rmt_transmit(tx_channel, copy_encoder, symbols,
                          symbol_count * sizeof(symbols[0]), &config);
    if (result == ESP_OK) transmission_active = true;
    return result;
}

esp_err_t ir_helper_send_pulses(const uint16_t *durations, size_t count)
{
    return send_pulses(durations, count, 50);
}

esp_err_t ir_helper_send(uint8_t addr, uint8_t cmd)
{
    /* Match the original NEC encoder exactly, including its final 32767us
       idle space. The four bytes are sent least significant bit first. */
    const uint32_t data = (uint32_t)addr | ((uint32_t)(uint8_t)~addr << 8) |
                          ((uint32_t)cmd << 16) | ((uint32_t)(uint8_t)~cmd << 24);
    uint16_t durations[68];
    durations[0] = 9000;
    durations[1] = 4500;
    for (size_t bit = 0; bit < 32; ++bit) {
        durations[2 + bit * 2] = 560;
        durations[3 + bit * 2] = (data & (UINT32_C(1) << bit)) ? 1690 : 560;
    }
    durations[66] = 560;
    durations[67] = 32767;
    esp_err_t result = send_pulses(durations, 68, 33);
    if (result != ESP_OK) return result;
    result = rmt_tx_wait_all_done(tx_channel, IR_NEC_TIMEOUT_MS);
    if (result == ESP_OK) {
        transmission_active = false;
        atomic_store_explicit(&transmission_done, false, memory_order_release);
    }
    return result;
}

esp_err_t ir_helper_cancel(void)
{
    if (!tx_channel && !copy_encoder) return ESP_OK;
    const gpio_num_t previous_pin = tx_pin;
    const esp_err_t result = release_resources();
    if (result != ESP_OK) return result;
    /* A complete teardown also discards completed/aborted driver queue entries.
       Recreate only a previously fully initialized channel. */
    return previous_pin == GPIO_NUM_NC ? ESP_OK : ir_helper_init(previous_pin);
}
