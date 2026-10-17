/* SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT */
#pragma once
#include <mooncake.h>
#include <hal/hal.h>
#include "record_storage.h"
#include <array>
#include <cstdint>
#include <vector>

class AppRecord : public mooncake::AppAbility {
public:
    AppRecord();
    ~AppRecord();
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
private:
    // Three reusable PCM blocks: one can be on disk while two belong to I2S.
    // Storage is constant (24 KiB) regardless of the recording duration.
    static constexpr size_t BLOCK_SAMPLES = 4096;
    static constexpr size_t BUFFER_COUNT = 3;
    static constexpr uint32_t SAMPLE_RATE = 16000;
    static constexpr uint32_t STALL_TIMEOUT_MS = 3000;
    enum class Phase { Library, Recording, Stopping, Playing, Error };
    Phase _phase = Phase::Error;
    int16_t* _pcm = nullptr;
    std::array<size_t, BUFFER_COUNT> _pending{};
    std::array<size_t, BUFFER_COUNT> _play_lengths{};
    size_t _pending_count = 0;
    size_t _next_buffer = 0;
    std::array<int16_t, 200> _wave{};
    std::vector<factory_record::Entry> _entries;
    size_t _selected = 0;
    std::string _directory;
    factory_record::WavWriter _writer;
    factory_record::WavReader _reader;
    m5::mic_config_t _saved_mic_config;
    uint8_t _saved_volume = 0;
    uint32_t _last_progress = 0, _last_draw = 0;
    uint32_t _played_samples = 0;
    uint32_t _max_sd_write_ms = 0;
    int32_t _peak = 0;
    bool _session_open = false, _close_after_save = false;
    bool refresh_library(const std::string& selectPath = "");
    void render_library();
    void render_audio();
    void start_recording();
    void stop_recording(bool closeAfter);
    void update_recording();
    bool queue_capture();
    void finish_recording();
    void start_playback();
    void update_playback();
    void stop_playback();
    size_t free_buffer();
    size_t pop_pending();
    void update_wave(const int16_t* data, size_t samples);
    void show_error(const char* message);
    void log_stage(const char* stage);
};
