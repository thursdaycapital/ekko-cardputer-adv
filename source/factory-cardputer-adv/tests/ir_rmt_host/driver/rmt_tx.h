#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"
typedef struct test_channel *rmt_channel_handle_t;
typedef struct test_encoder *rmt_encoder_handle_t;
typedef struct { size_t num_symbols; } rmt_tx_done_event_data_t;
typedef bool (*rmt_tx_done_callback_t)(rmt_channel_handle_t, const rmt_tx_done_event_data_t *, void *);
typedef struct { rmt_tx_done_callback_t on_trans_done; } rmt_tx_event_callbacks_t;
typedef union {
    struct {
        uint32_t duration0 : 15;
        uint32_t level0 : 1;
        uint32_t duration1 : 15;
        uint32_t level1 : 1;
    };
    uint32_t val;
} rmt_symbol_word_t;
#define RMT_CLK_SRC_DEFAULT 0
typedef struct {
    int clk_src;
    uint32_t resolution_hz;
    size_t mem_block_symbols;
    size_t trans_queue_depth;
    gpio_num_t gpio_num;
} rmt_tx_channel_config_t;
typedef struct {
    float duty_cycle;
    uint32_t frequency_hz;
} rmt_carrier_config_t;
typedef struct {
    int loop_count;
    struct {
        uint32_t eot_level : 1;
        uint32_t queue_nonblocking : 1;
    } flags;
} rmt_transmit_config_t;
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *, rmt_channel_handle_t *);
esp_err_t rmt_apply_carrier(rmt_channel_handle_t, const rmt_carrier_config_t *);
esp_err_t rmt_enable(rmt_channel_handle_t);
esp_err_t rmt_tx_register_event_callbacks(rmt_channel_handle_t, const rmt_tx_event_callbacks_t *, void *);
esp_err_t rmt_disable(rmt_channel_handle_t);
esp_err_t rmt_del_channel(rmt_channel_handle_t);
esp_err_t rmt_transmit(rmt_channel_handle_t, rmt_encoder_handle_t, const void *, size_t, const rmt_transmit_config_t *);
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t, int);
