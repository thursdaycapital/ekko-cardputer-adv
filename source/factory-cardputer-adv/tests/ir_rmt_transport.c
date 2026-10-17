#include "ir_helper.h"
#include "driver/rmt_encoder.h"
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct test_channel {
    bool enabled;
    bool queued;
    bool done;
    rmt_tx_event_callbacks_t callbacks;
    void *context;
};
struct test_encoder { int valid; };
enum Failure { NEW_CHANNEL, CARRIER, NEW_ENCODER, REGISTER_CALLBACK, ENABLE, DISABLE, DELETE_CHANNEL,
               DELETE_ENCODER, TRANSMIT, WAIT, FAILURE_COUNT };
static unsigned failures;
static int channels, encoders, allocations, transmissions, carrier_calls, wait_calls, callback_calls;
static bool complete_on_blocking_wait, complete_inside_transmit;
static rmt_channel_handle_t current_channel;
static float last_duty;
static int last_timeout;
static const rmt_symbol_word_t *last_payload;
static size_t last_bytes;
static rmt_tx_channel_config_t last_channel_config;

static bool fail(enum Failure stage)
{
    const unsigned bit = 1u << stage;
    if (!(failures & bit)) return false;
    failures &= ~bit;
    return true;
}

esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *config, rmt_channel_handle_t *out)
{
    if (fail(NEW_CHANNEL)) return ESP_ERR_NO_MEM;
    *out = calloc(1, sizeof(**out));
    assert(*out);
    ++channels;
    ++allocations;
    current_channel = *out;
    last_channel_config = *config;
    return ESP_OK;
}
esp_err_t rmt_apply_carrier(rmt_channel_handle_t channel, const rmt_carrier_config_t *config)
{
    assert(channel && !channel->queued);
    if (fail(CARRIER)) return ESP_FAIL;
    assert(config->frequency_hz == 38000);
    last_duty = config->duty_cycle;
    ++carrier_calls;
    return ESP_OK;
}
esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t *config, rmt_encoder_handle_t *out)
{
    assert(config);
    if (fail(NEW_ENCODER)) return ESP_ERR_NO_MEM;
    *out = malloc(sizeof(**out));
    assert(*out);
    (*out)->valid = 1;
    ++encoders;
    return ESP_OK;
}
esp_err_t rmt_tx_register_event_callbacks(rmt_channel_handle_t channel,
                                         const rmt_tx_event_callbacks_t *callbacks, void *context)
{
    assert(channel && !channel->enabled && callbacks && callbacks->on_trans_done);
    if (fail(REGISTER_CALLBACK)) return ESP_FAIL;
    channel->callbacks = *callbacks;
    channel->context = context;
    return ESP_OK;
}
esp_err_t rmt_enable(rmt_channel_handle_t channel)
{
    assert(channel && !channel->enabled && channel->callbacks.on_trans_done);
    if (fail(ENABLE)) return ESP_FAIL;
    channel->enabled = true;
    return ESP_OK;
}
esp_err_t rmt_disable(rmt_channel_handle_t channel)
{
    assert(channel && channel->enabled);
    if (fail(DISABLE)) return ESP_FAIL;
    channel->queued = false;
    channel->done = false;
    channel->enabled = false;
    return ESP_OK;
}
esp_err_t rmt_del_channel(rmt_channel_handle_t channel)
{
    assert(channel && !channel->enabled && !channel->queued);
    if (fail(DELETE_CHANNEL)) return ESP_FAIL;
    assert(current_channel == channel);
    current_channel = NULL;
    free(channel);
    --channels;
    return ESP_OK;
}
esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder)
{
    assert(encoder && encoder->valid);
    if (fail(DELETE_ENCODER)) return ESP_FAIL;
    free(encoder);
    --encoders;
    return ESP_OK;
}
static void complete_hardware_transaction(rmt_channel_handle_t channel)
{
    assert(channel && channel->enabled && channel->queued && !channel->done);
    channel->done = true; /* Driver enqueues its completed transaction before the callback. */
    const rmt_tx_done_event_data_t event = {.num_symbols = last_bytes / sizeof(*last_payload) + 1};
    assert(!channel->callbacks.on_trans_done(channel, &event, channel->context));
    ++callback_calls;
}
esp_err_t rmt_transmit(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder,
                       const void *payload, size_t bytes, const rmt_transmit_config_t *config)
{
    assert(channel && channel->enabled && !channel->queued);
    assert(encoder && encoder->valid && config->loop_count == 0);
    assert(config->flags.queue_nonblocking == 1 && config->flags.eot_level == 0);
    if (fail(TRANSMIT)) return ESP_FAIL;
    channel->queued = true;
    channel->done = false;
    last_payload = payload; /* Deliberately retain the production pointer, not a copy. */
    last_bytes = bytes;
    ++transmissions;
    if (complete_inside_transmit) complete_hardware_transaction(channel);
    return ESP_OK;
}
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t channel, int timeout)
{
    assert(channel && timeout >= 0 && timeout <= 250);
    ++wait_calls;
    last_timeout = timeout;
    if (fail(WAIT)) return ESP_FAIL;
    if (!channel->queued) return ESP_OK;
    if (!channel->done && complete_on_blocking_wait && timeout > 0) complete_hardware_transaction(channel);
    if (!channel->done) return ESP_ERR_TIMEOUT;
    channel->queued = false;
    channel->done = false;
    return ESP_OK;
}

static void fixture(void)
{
    failures = 0;
    complete_on_blocking_wait = false;
    complete_inside_transmit = false;
    assert(ir_helper_cancel() == ESP_OK);
    assert(ir_helper_init(44) == ESP_OK);
    assert(channels == 1 && encoders == 1);
}
static void finish(void)
{
    if (current_channel && current_channel->queued && !current_channel->done)
        complete_hardware_transaction(current_channel);
    assert(ir_helper_poll() == ESP_OK);
    assert(last_timeout == 0);
}
static void check_half(size_t index, uint16_t duration, bool level)
{
    assert(index / 2 < last_bytes / sizeof(*last_payload));
    const rmt_symbol_word_t symbol = last_payload[index / 2];
    assert((index & 1 ? symbol.duration1 : symbol.duration0) == duration);
    assert((index & 1 ? symbol.level1 : symbol.level0) == (unsigned)level);
}
static void raw_from_expired_stack(void)
{
    const uint16_t pulses[] = {4500, 4500, 560, 1690, 560};
    assert(ir_helper_send_pulses(pulses, 5) == ESP_OK);
}
static void test_initial_and_idempotent(void)
{
    assert(ir_helper_poll() == ESP_OK);
    assert(ir_helper_cancel() == ESP_OK);
    const uint16_t pulse[] = {10, 20};
    assert(ir_helper_send_pulses(pulse, 2) == ESP_ERR_INVALID_STATE);
    assert(ir_helper_init(-1) == ESP_ERR_INVALID_ARG);
    assert(ir_helper_init(46) == ESP_ERR_INVALID_ARG);
    assert(!channels && !encoders);
    assert(ir_helper_init(44) == ESP_OK);
    const int before = allocations;
    assert(ir_helper_init(44) == ESP_OK);
    assert(allocations == before);
    assert(last_channel_config.resolution_hz == 1000000);
    assert(last_channel_config.trans_queue_depth == 1);
    assert(last_channel_config.mem_block_symbols == 64);
    assert(fabsf(last_duty - .33f) < .00001f);
}
static void test_async_payload_lifetime(void)
{
    fixture();
    raw_from_expired_stack();
    volatile unsigned char overwrite[8192];
    memset((void *)overwrite, 0xA5, sizeof(overwrite));
    assert(last_bytes == 3 * sizeof(rmt_symbol_word_t));
    check_half(0, 4500, true);
    check_half(1, 4500, false);
    check_half(2, 560, true);
    check_half(3, 1690, false);
    check_half(4, 560, true);
    check_half(5, 0, false);
    assert(fabsf(last_duty - .50f) < .00001f);
    assert(ir_helper_poll() == ESP_ERR_TIMEOUT);
    finish();
}
static void test_busy_and_poll_failure_preserve_payload(void)
{
    fixture();
    uint16_t pulses[] = {9000, 4500};
    assert(ir_helper_send_pulses(pulses, 2) == ESP_OK);
    pulses[0] = 1;
    check_half(0, 9000, true);
    const int before = transmissions;
    const int carriers_before = carrier_calls;
    assert(ir_helper_send_pulses(pulses, 2) == ESP_ERR_TIMEOUT);
    assert(ir_helper_send(0, 0) == ESP_ERR_TIMEOUT);
    assert(ir_helper_init(43) == ESP_ERR_INVALID_STATE);
    assert(ir_helper_init(44) == ESP_OK);
    assert(transmissions == before && carrier_calls == carriers_before);
    check_half(0, 9000, true);
    const int waits_before = wait_calls;
    for (size_t i = 0; i < 1000; ++i) assert(ir_helper_poll() == ESP_ERR_TIMEOUT);
    assert(wait_calls == waits_before); /* No benign driver timeout/ERROR logging. */
    complete_hardware_transaction(current_channel);
    failures = 1u << WAIT;
    assert(ir_helper_poll() == ESP_FAIL);
    failures = 1u << WAIT;
    assert(ir_helper_send_pulses(pulses, 2) == ESP_FAIL);
    check_half(0, 9000, true);
    finish();
    assert(ir_helper_send_pulses(pulses, 2) == ESP_OK);
    check_half(0, 1, true);
    finish();
}
static void test_zero_and_split_timings(void)
{
    fixture();
    const uint16_t pulses[] = {0, 100, 200, 0, 300, 65535, 0, 1, 32768};
    assert(ir_helper_send_pulses(pulses, 9) == ESP_OK);
    const uint16_t durations[] = {100, 200, 300, 32767, 32767, 1, 1, 32767, 1, 0};
    const bool levels[] = {false, true, true, false, false, false, false, true, true, false};
    assert(last_bytes == 5 * sizeof(rmt_symbol_word_t));
    for (size_t i = 0; i < 10; ++i) check_half(i, durations[i], levels[i]);
    finish();
}
static void test_size_and_invalid_input(void)
{
    fixture();
    uint16_t pulses[2049];
    for (size_t i = 0; i < 2049; ++i) pulses[i] = 1;
    assert(ir_helper_send_pulses(pulses, 2048) == ESP_OK);
    assert(last_bytes == 4096);
    finish();
    const int before = transmissions;
    assert(ir_helper_send_pulses(pulses, 2049) == ESP_ERR_INVALID_SIZE);
    assert(ir_helper_send_pulses(NULL, 2) == ESP_ERR_INVALID_ARG);
    assert(ir_helper_send_pulses(pulses, 0) == ESP_ERR_INVALID_ARG);
    pulses[0] = pulses[1] = 0;
    assert(ir_helper_send_pulses(pulses, 2) == ESP_ERR_INVALID_ARG);
    assert(transmissions == before);
    check_half(0, 1, true);
}
static void test_nec_vector_and_carrier_switch(void)
{
    fixture();
    raw_from_expired_stack();
    finish();
    complete_on_blocking_wait = true;
    for (unsigned address = 0; address < 256; ++address) {
        const uint8_t command = (uint8_t)(address * 37);
        assert(ir_helper_send((uint8_t)address, command) == ESP_OK);
        assert(last_timeout == 250 && last_bytes == 34 * sizeof(rmt_symbol_word_t));
        assert(fabsf(last_duty - .33f) < .00001f);
        check_half(0, 9000, true);
        check_half(1, 4500, false);
        const uint8_t expected[] = {(uint8_t)address, (uint8_t)~address, command, (uint8_t)~command};
        for (size_t byte = 0; byte < 4; ++byte)
            for (size_t bit = 0; bit < 8; ++bit) {
                const size_t half = 2 + (byte * 8 + bit) * 2;
                check_half(half, 560, true);
                check_half(half + 1, expected[byte] & (1u << bit) ? 1690 : 560, false);
            }
        check_half(66, 560, true);
        check_half(67, 32767, false);
    }
    raw_from_expired_stack();
    assert(fabsf(last_duty - .50f) < .00001f);
    finish();
}
static void test_nec_timeout_and_failed_cancel(void)
{
    fixture();
    assert(ir_helper_send(0x12, 0x34) == ESP_ERR_TIMEOUT);
    assert(last_timeout == 250);
    check_half(0, 9000, true);
    failures = 1u << DISABLE;
    assert(ir_helper_cancel() == ESP_FAIL);
    assert(ir_helper_poll() == ESP_ERR_TIMEOUT);
    check_half(0, 9000, true);
    assert(channels == 1 && encoders == 1);
    assert(ir_helper_cancel() == ESP_OK);
    assert(channels == 1 && encoders == 1 && ir_helper_poll() == ESP_OK);
    raw_from_expired_stack();
    finish();
}
static void test_initialization_failures(void)
{
    const enum Failure stages[] = {NEW_CHANNEL, CARRIER, NEW_ENCODER, REGISTER_CALLBACK, ENABLE};
    for (size_t i = 0; i < sizeof(stages) / sizeof(stages[0]); ++i) {
        fixture();
        failures = 1u << stages[i];
        assert(ir_helper_init(43) != ESP_OK);
        assert(channels == 0 && encoders == 0);
        assert(ir_helper_init(44) == ESP_OK);
        assert(channels == 1 && encoders == 1);
        raw_from_expired_stack();
        finish();
    }
}
static void test_cleanup_failure_retry(void)
{
    fixture();
    failures = 1u << NEW_CHANNEL;
    assert(ir_helper_init(43) == ESP_ERR_NO_MEM);
    assert(channels == 0 && encoders == 0);
    failures = (1u << CARRIER) | (1u << DELETE_CHANNEL);
    assert(ir_helper_init(43) == ESP_FAIL);
    assert(channels == 1 && encoders == 0);
    assert(ir_helper_init(44) == ESP_OK);
    assert(channels == 1 && encoders == 1);
    failures = 1u << DELETE_ENCODER;
    assert(ir_helper_init(43) == ESP_FAIL);
    assert(channels == 0 && encoders == 1);
    assert(ir_helper_init(44) == ESP_OK);
    assert(channels == 1 && encoders == 1);
    failures = 1u << DELETE_CHANNEL;
    assert(ir_helper_cancel() == ESP_FAIL);
    assert(channels == 1 && encoders == 1);
    assert(ir_helper_init(44) == ESP_OK);
    raw_from_expired_stack();
    finish();
}
static void test_transmit_and_carrier_failure_retry(void)
{
    fixture();
    const uint16_t pulses[] = {100, 200};
    failures = 1u << CARRIER;
    const int before = transmissions;
    assert(ir_helper_send_pulses(pulses, 2) == ESP_FAIL);
    assert(transmissions == before);
    failures = 1u << TRANSMIT;
    assert(ir_helper_send_pulses(pulses, 2) == ESP_FAIL);
    assert(ir_helper_poll() == ESP_OK);
    assert(ir_helper_send_pulses(pulses, 2) == ESP_OK);
    finish();
    complete_on_blocking_wait = true;
    failures = 1u << CARRIER;
    assert(ir_helper_send(1, 2) == ESP_FAIL);
    assert(ir_helper_send(1, 2) == ESP_OK);
    assert(fabsf(last_duty - .33f) < .00001f);
}
static void test_cancel_reinitialization_failure(void)
{
    fixture();
    raw_from_expired_stack();
    failures = 1u << NEW_ENCODER;
    assert(ir_helper_cancel() == ESP_ERR_NO_MEM);
    assert(channels == 0 && encoders == 0);
    assert(ir_helper_poll() == ESP_OK);
    assert(ir_helper_init(44) == ESP_OK);
    raw_from_expired_stack();
    finish();
}
static void test_completion_before_transmit_returns(void)
{
    fixture();
    complete_inside_transmit = true;
    const int callbacks_before = callback_calls;
    const int allocations_before = allocations;
    raw_from_expired_stack();
    assert(callback_calls == callbacks_before + 1 && allocations == allocations_before);
    const int waits_before = wait_calls;
    assert(ir_helper_poll() == ESP_OK);
    assert(wait_calls == waits_before + 1);
    assert(ir_helper_poll() == ESP_OK && wait_calls == waits_before + 1);

    /* The next transaction must not inherit the previous completion flag. */
    complete_inside_transmit = false;
    raw_from_expired_stack();
    assert(ir_helper_poll() == ESP_ERR_TIMEOUT && wait_calls == waits_before + 1);
    finish();
}
static void test_legacy_timeout_reap_and_cancel_flag_reset(void)
{
    fixture();
    assert(ir_helper_send(0x12, 0x34) == ESP_ERR_TIMEOUT);
    assert(last_timeout == 250);
    complete_hardware_transaction(current_channel);
    complete_on_blocking_wait = true;
    const int waits_before = wait_calls;
    assert(ir_helper_send(0x56, 0x78) == ESP_OK);
    assert(wait_calls == waits_before + 2 && last_timeout == 250);

    raw_from_expired_stack();
    const int waits_after_legacy = wait_calls;
    assert(ir_helper_poll() == ESP_ERR_TIMEOUT && wait_calls == waits_after_legacy);
    complete_hardware_transaction(current_channel);
    assert(ir_helper_cancel() == ESP_OK);
    raw_from_expired_stack();
    assert(ir_helper_poll() == ESP_ERR_TIMEOUT && wait_calls == waits_after_legacy);
    finish();
}
int main(void)
{
    test_initial_and_idempotent();
    test_async_payload_lifetime();
    test_busy_and_poll_failure_preserve_payload();
    test_zero_and_split_timings();
    test_size_and_invalid_input();
    test_nec_vector_and_carrier_switch();
    test_nec_timeout_and_failed_cancel();
    test_initialization_failures();
    test_cleanup_failure_retry();
    test_transmit_and_carrier_failure_retry();
    test_cancel_reinitialization_failure();
    test_completion_before_transmit_returns();
    test_legacy_timeout_reap_and_cancel_flag_reset();
    /* Exercise a complete deallocation path before process exit. */
    failures = 1u << NEW_CHANNEL;
    assert(ir_helper_init(43) == ESP_ERR_NO_MEM);
    assert(channels == 0 && encoders == 0);
    puts("PASS: 13 native RMT transport groups, 256 NEC vectors, ASan/UBSan");
    return 0;
}
