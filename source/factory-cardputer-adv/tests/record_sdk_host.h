#pragma once

// Minimal hardware/FreeRTOS declarations for compiling unmodified SDK methods.
// No microphone or speaker lifecycle implementation is reproduced here.
#include <algorithm>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>
#include <alloca.h>

#define CONFIG_IDF_TARGET_ESP32S3 1
#define SOC_I2S_NUM 1
#define portNUM_PROCESSORS 2
#define portTICK_PERIOD_MS 1
#define SAMPLERATE_MUL 256
constexpr int pdPASS = 1;
constexpr int pdTRUE = 1;
constexpr unsigned portMAX_DELAY = UINT_MAX;
constexpr int ESP_OK = 0;
using BaseType_t = int;
using esp_err_t = int;
using SemaphoreHandle_t = void*;
using TaskEntry = void(*)(void*);
struct FakeTask { TaskEntry entry; void* argument; bool completed = false; };
using TaskHandle_t = FakeTask*;

namespace sdk_test {
struct Runtime {
    bool fail_semaphore = false;
    bool fail_task = false;
    int setup_result = ESP_OK;
    unsigned setups = 0;
    unsigned uninstalls = 0;
    unsigned enabled_calls = 0;
    unsigned disabled_calls = 0;
    bool callback_result = true;
    unsigned pinned_creations = 0;
    unsigned plain_creations = 0;
    unsigned read_calls = 0;
    unsigned stopped = 0;
    unsigned deleted_tasks = 0;
    unsigned delay_calls = 0;
    unsigned notify_waits = 0;
    size_t read_result_bytes = 16;
    int read_result = ESP_OK;
    std::function<void()> on_read;
    std::function<void()> on_wait;
    std::vector<std::unique_ptr<FakeTask>> tasks;
};
extern Runtime runtime;
inline bool callback(void*, bool enabled) {
    if (enabled) ++runtime.enabled_calls;
    else ++runtime.disabled_calls;
    return !enabled || runtime.callback_result;
}
}

inline SemaphoreHandle_t xSemaphoreCreateBinary() {
    static int semaphore;
    return sdk_test::runtime.fail_semaphore ? nullptr : &semaphore;
}
inline void xSemaphoreGive(SemaphoreHandle_t) {}
inline void xSemaphoreTake(SemaphoreHandle_t, unsigned) {
    throw std::runtime_error("unexpected blocking SDK semaphore wait");
}
inline BaseType_t createTask(TaskEntry entry, void* argument, TaskHandle_t* handle) {
    if (sdk_test::runtime.fail_task) { *handle = nullptr; return 0; }
    auto task = std::make_unique<FakeTask>(FakeTask{entry, argument});
    *handle = task.get();
    sdk_test::runtime.tasks.push_back(std::move(task));
    return pdPASS;
}
inline BaseType_t xTaskCreate(TaskEntry entry, const char*, size_t, void* argument, unsigned, TaskHandle_t* handle) {
    ++sdk_test::runtime.plain_creations;
    return createTask(entry, argument, handle);
}
inline BaseType_t xTaskCreatePinnedToCore(TaskEntry entry, const char*, size_t, void* argument, unsigned,
                                        TaskHandle_t* handle, unsigned) {
    ++sdk_test::runtime.pinned_creations;
    return createTask(entry, argument, handle);
}
inline void xTaskNotifyGive(TaskHandle_t) {}
inline void vTaskDelay(unsigned) {
    if (++sdk_test::runtime.delay_calls > 8) throw std::runtime_error("SDK task failed to finish during join");
    for (const auto& task : sdk_test::runtime.tasks) {
        if (!task->completed) {
            task->completed = true;
            task->entry(task->argument);
        }
    }
}
inline unsigned ulTaskNotifyTake(int, unsigned) {
    ++sdk_test::runtime.notify_waits;
    if (!sdk_test::runtime.on_wait) throw std::runtime_error("SDK task unexpectedly waited forever");
    sdk_test::runtime.on_wait();
    return 0;
}
inline void vTaskDelete(void*) { ++sdk_test::runtime.deleted_tasks; }

namespace m5 {
enum class i2s_port_t { I2S_NUM_0 = 0, I2S_NUM_1 = 1 };
struct FakeI2s {
    struct { unsigned rx_pdm_en = 0, rx_tdm_en = 0, rx_update = 0; } rx_conf;
    struct { unsigned rx_bck_div_num = 0; } rx_conf1;
};
inline FakeI2s I2S0;
inline esp_err_t _i2s_start(i2s_port_t) { return ESP_OK; }
inline esp_err_t _i2s_stop(i2s_port_t) { ++sdk_test::runtime.stopped; return ESP_OK; }
inline esp_err_t _i2s_driver_uninstall(i2s_port_t) { ++sdk_test::runtime.uninstalls; return ESP_OK; }
inline esp_err_t _i2s_read(i2s_port_t, void* output, size_t bytes, size_t* result, unsigned) {
    ++sdk_test::runtime.read_calls;
    if (sdk_test::runtime.read_calls > 12) throw std::runtime_error("microphone cancellation did not break the capture loop");
    std::memset(output, 0, bytes);
    if (sdk_test::runtime.on_read) sdk_test::runtime.on_read();
    *result = std::min(bytes, sdk_test::runtime.read_result_bytes);
    return sdk_test::runtime.read_result;
}
inline void calcClockDiv(uint32_t* a, uint32_t* b, uint32_t* n, uint32_t, uint32_t) {
    *a = 1; *b = 0; *n = 1;
}

struct mic_config_t {
    int pin_bck = 41;
    uint32_t sample_rate = 16000;
    uint8_t over_sampling = 1;
    uint8_t magnification = 16;
    uint8_t noise_filter_level = 0;
    bool use_adc = false;
    bool stereo = false;
    size_t dma_buf_len = 128;
    unsigned task_priority = 2;
    unsigned task_pinned_core = UINT_MAX;
    i2s_port_t i2s_port = i2s_port_t::I2S_NUM_0;
};

class Mic_Class {
public:
    struct recording_info_t {
        void* data = nullptr;
        size_t length = 0;
        size_t index = 0;
        bool is_stereo = false;
        bool is_16bit = false;
    };
    recording_info_t _rec_info[2];
    volatile bool _rec_flip = false;
    mic_config_t _cfg;
    uint32_t _rec_sample_rate = 0;
    int32_t _offset = 0;
    volatile bool _task_running = false;
    volatile bool _is_recording = false;
    TaskHandle_t _task_handle = nullptr;
    volatile SemaphoreHandle_t _task_semaphore = nullptr;
    bool (*_cb_set_enabled)(void*, bool) = sdk_test::callback;
    void* _cb_set_enabled_args = nullptr;
    size_t isRecording(void) const;
    size_t recordingQueueSize(void) const;
    bool canRecord(void) const;
    esp_err_t _setup_i2s() { ++sdk_test::runtime.setups; return sdk_test::runtime.setup_result; }
    uint32_t _calc_rec_rate() const;
    static void mic_task(void*);
    bool begin();
    void end();
    bool _rec_raw(void*, size_t, bool, uint32_t, bool);
};

struct speaker_config_t {
    size_t dma_buf_len = 128;
    unsigned task_priority = 2;
    unsigned task_pinned_core = UINT_MAX;
    i2s_port_t i2s_port = i2s_port_t::I2S_NUM_1;
};

class Speaker_Class {
public:
    static constexpr size_t sound_channel_max = 8;
    struct wav_info_t {
        uint32_t repeat = 0;
        uint32_t sample_rate_x256 = 0;
        const void* data = nullptr;
        size_t length = 0;
        union {
            uint8_t flg = 0;
            struct {
                uint8_t is_stereo : 1;
                uint8_t is_16bit : 1;
                uint8_t is_signed : 1;
                uint8_t stop_current : 1;
                uint8_t no_clear_index : 1;
            };
        };
        void clear();
        void assign(const wav_info_t&);
    };
    struct channel_info_t { wav_info_t wavinfo[2]; volatile bool flip = false; };
    channel_info_t _ch_info[sound_channel_max];
    speaker_config_t _cfg;
    volatile bool _task_running = false;
    std::atomic<uint16_t> _play_channel_bits{0};
    TaskHandle_t _task_handle = nullptr;
    volatile SemaphoreHandle_t _task_semaphore = nullptr;
    bool (*_cb_set_enabled)(void*, bool) = sdk_test::callback;
    void* _cb_set_enabled_args = nullptr;
    static void spk_task(void* argument) {
        auto self = static_cast<Speaker_Class*>(argument);
        self->_task_handle = nullptr;
        vTaskDelete(nullptr);
    }
    esp_err_t _setup_i2s() { ++sdk_test::runtime.setups; return sdk_test::runtime.setup_result; }
    bool begin();
    void end();
    void stop();
    bool _set_next_wav(size_t, const wav_info_t&);
    bool _play_raw(const void*, size_t, bool, bool, float, bool, uint32_t, int, bool, bool);
    size_t isPlaying(uint8_t channel) const volatile;
    bool canPlay(uint8_t channel) const volatile;
};
}
