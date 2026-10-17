/* SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT */
#include "app_record.h"
#include "assets/record_big.h"
#include "assets/record_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <new>

using namespace mooncake;

AppRecord::AppRecord()
{
    setAppInfo().name = "Record";
    setAppInfo().userData = new AppIcon_t(image_data_record_big, image_data_record_small);
}
AppRecord::~AppRecord()
{
    if (_session_open) onClose();
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}
void AppRecord::log_stage(const char* stage)
{
    mclog::tagInfo(getAppInfo().name, "{} free={} largest={} bytes={} pending={} peak={} mic={} speaker={}",
        stage, heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        _writer.dataBytes(), _pending_count, _peak, GetHAL().mic.recordingQueueSize(), GetHAL().speaker.isPlaying(0));
}
void AppRecord::onOpen()
{
    if (_session_open) onClose();
    _session_open = true;
    _saved_mic_config = GetHAL().mic.config();
    _saved_volume = GetHAL().speaker.getVolume();
    _pending_count = 0;
    _next_buffer = 0;
    _close_after_save = false;
    _selected = 0;
    // Stop entry-key cues before releasing their retained PCM or allocating ours.
    GetHAL().speaker.end();
    audio::set_keyboard_sfx_enable(false);
    GetHAL().canvas.setFont(FONT_BASIC);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setTextScroll(false);
    if (!GetHAL().ensureSdCardMounted()) {
        show_error("未检测到可用的SD卡");
        return;
    }
    _directory = std::string(GetHAL().sdCardMountPoint()) + "/RECORD";
    _pcm = new (std::nothrow) int16_t[BLOCK_SAMPLES * BUFFER_COUNT]();
    if (!_pcm) {
        show_error("内存不足，无法录音");
        return;
    }
    if (refresh_library()) { _phase = Phase::Library; render_library(); }
    log_stage("sd_library_open");
}
bool AppRecord::refresh_library(const std::string& selectPath)
{
    std::string error;
    _entries = factory_record::listRecordings(_directory, &error);
    if (!error.empty()) {
        mclog::tagError(getAppInfo().name, "list: {}", error);
        show_error("SD卡目录读取失败");
        return false;
    }
    _selected = _entries.empty() ? 0 : std::min(_selected, _entries.size() - 1);
    for (size_t i = 0; i < _entries.size(); ++i) {
        if (_entries[i].path == selectPath) { _selected = i; break; }
    }
    return true;
}
void AppRecord::onRunning()
{
    if (GetHAL().homeButton.wasClicked()) {
        if (_phase == Phase::Recording || _phase == Phase::Stopping) stop_recording(true);
        else close();
        return;
    }
    if (_phase == Phase::Recording || _phase == Phase::Stopping) update_recording();
    else if (_phase == Phase::Playing) update_playback();
    if (!_session_open) return;
    const auto key = GetHAL().keyboard.getLatestKeyEvent();
    if (!key.state) return;
    if (_phase == Phase::Library) {
        if (key.keyCode == KEY_N) start_recording();
        else if (key.keyCode == KEY_ENTER) {
            if (_entries.empty()) start_recording(); else start_playback();
        } else if (!_entries.empty()) {
            if (key.keyCode == KEY_UP || key.keyCode == KEY_SEMICOLON) {
                _selected = (_selected + _entries.size() - 1) % _entries.size();
                render_library();
            } else if (key.keyCode == KEY_DOWN || key.keyCode == KEY_DOT) {
                _selected = (_selected + 1) % _entries.size();
                render_library();
            }
        }
    } else if (key.keyCode == KEY_ENTER) {
        if (_phase == Phase::Recording) stop_recording(false);
        else if (_phase == Phase::Playing) stop_playback();
        else if (_phase == Phase::Error) { onClose(); onOpen(); }
    }
}
void AppRecord::onClose()
{
    if (!_session_open) return;
    // Mic and Speaker tasks must release every PCM pointer before free().
    GetHAL().mic.end();
    GetHAL().speaker.stop();
    GetHAL().speaker.end();
    _reader.close();
    if (_writer.active() && !_writer.finish()) {
        mclog::tagError(getAppInfo().name, "close save: {}", _writer.error());
    }
    delete[] _pcm;
    _pcm = nullptr;
    _pending_count = 0;
    _entries.clear();
    GetHAL().mic.config(_saved_mic_config);
    GetHAL().speaker.setVolume(_saved_volume);
    if (!GetHAL().speaker.begin()) log_stage("close_speaker_failed");
    _session_open = false;
    _phase = Phase::Error;
    audio::set_keyboard_sfx_enable(true);
    log_stage("sd_closed");
}
size_t AppRecord::free_buffer()
{
    for (size_t i = 0; i < BUFFER_COUNT; ++i) {
        const size_t candidate = (_next_buffer + i) % BUFFER_COUNT;
        bool busy = false;
        for (size_t j = 0; j < _pending_count; ++j) busy |= _pending[j] == candidate;
        if (!busy) { _next_buffer = (candidate + 1) % BUFFER_COUNT; return candidate; }
    }
    return BUFFER_COUNT;
}
size_t AppRecord::pop_pending()
{
    const size_t value = _pending[0];
    for (size_t i = 1; i < _pending_count; ++i) _pending[i - 1] = _pending[i];
    --_pending_count;
    return value;
}
void AppRecord::start_recording()
{
    if (!_pcm) { show_error("内存不足，无法录音"); return; }
    GetHAL().speaker.end();
    auto cfg = _saved_mic_config;
    cfg.magnification = 128;
    cfg.noise_filter_level = 2;
    GetHAL().mic.config(cfg);
    if (!GetHAL().mic.begin() || !GetHAL().mic.isRunning()) { show_error("麦克风启动失败"); return; }
    // Do not create an empty recording when the microphone cannot start.
    if (!_writer.open(_directory)) {
        mclog::tagError(getAppInfo().name, "open: {}", _writer.error());
        show_error("无法新建录音，请检查SD卡");
        return;
    }
    _pending_count = 0;
    _peak = 0;
    _max_sd_write_ms = 0;
    _wave.fill(0);
    _close_after_save = false;
    _phase = Phase::Recording;
    _last_progress = _last_draw = GetHAL().millis();
    mclog::tagInfo(getAppInfo().name, "record_file {}", _writer.path());
    log_stage("sd_recording");
    render_audio();
}
void AppRecord::stop_recording(bool closeAfter)
{
    _close_after_save |= closeAfter;
    if (_phase == Phase::Stopping) return;
    _phase = Phase::Stopping;
    _last_progress = GetHAL().millis();
    render_audio();
    log_stage("sd_stopping");
}
void AppRecord::update_recording()
{
    if (!GetHAL().mic.isRunning()) { show_error("麦克风已停止，请重试"); return; }
    // Count published descriptors, including those not yet picked up by I2S.
    // The SDK's original isRecording() alone can return zero before task start.
    const size_t busy = GetHAL().mic.recordingQueueSize();
    if (busy > _pending_count) { show_error("录音缓冲状态异常"); return; }
    size_t completed = _pending_count - busy;
    while (completed--) {
        // The completed block stays reserved until its data reaches disk.
        // Feed the third block to I2S first, so SD sync can overlap capture.
        if (_phase == Phase::Recording && !queue_capture()) return;
        const auto* data = _pcm + pop_pending() * BLOCK_SAMPLES;
        update_wave(data, BLOCK_SAMPLES);
        const uint32_t write_start = GetHAL().millis();
        if (!_writer.append(data, BLOCK_SAMPLES)) {
            mclog::tagError(getAppInfo().name, "write: {}", _writer.error());
            show_error("写入失败，请检查SD卡空间");
            return;
        }
        _max_sd_write_ms = std::max(_max_sd_write_ms, GetHAL().millis() - write_start);
        _last_progress = GetHAL().millis();
    }
    if (_phase == Phase::Stopping && !_pending_count) { finish_recording(); return; }
    if (_phase == Phase::Recording && !queue_capture()) return;
    const uint32_t now = GetHAL().millis();
    if (now - _last_progress >= STALL_TIMEOUT_MS) { show_error("录音停止响应，请重试"); return; }
    if (now - _last_draw >= 100) { _last_draw = now; render_audio(); }
}
bool AppRecord::queue_capture()
{
    if (_pending_count >= BUFFER_COUNT || GetHAL().mic.recordingQueueSize() >= 2 || !GetHAL().mic.canRecord()) return true;
    const size_t index = free_buffer();
    if (index == BUFFER_COUNT || !GetHAL().mic.record(_pcm + index * BLOCK_SAMPLES, BLOCK_SAMPLES, SAMPLE_RATE)) {
        show_error("录音采集失败"); return false;
    }
    _pending[_pending_count++] = index;
    return true;
}
void AppRecord::finish_recording()
{
    GetHAL().mic.end();
    if (!_writer.finish()) {
        mclog::tagError(getAppInfo().name, "save: {}", _writer.error());
        show_error("保存失败，请检查SD卡"); return;
    }
    mclog::tagInfo(getAppInfo().name, "saved {} bytes={} peak={} max_sd_write_ms={}",
        _writer.path(), _writer.dataBytes(), _peak, _max_sd_write_ms);
    if (_close_after_save) { close(); return; }
    if (refresh_library(_writer.path())) { _phase = Phase::Library; render_library(); }
}
void AppRecord::start_playback()
{
    if (_entries.empty() || !_pcm) return;
    if (!_reader.open(_entries[_selected].path)) {
        mclog::tagError(getAppInfo().name, "read open: {}", _reader.error());
        show_error("录音文件读取失败"); return;
    }
    GetHAL().mic.end();
    if (!GetHAL().speaker.begin() || !GetHAL().speaker.isRunning()) { show_error("扬声器启动失败"); return; }
    GetHAL().speaker.setVolume(_saved_volume);
    _pending_count = 0;
    _played_samples = 0;
    _peak = 0;
    _wave.fill(0);
    _phase = Phase::Playing;
    _last_progress = _last_draw = GetHAL().millis();
    log_stage("sd_playing");
    render_audio();
}
void AppRecord::update_playback()
{
    const size_t busy = GetHAL().speaker.isPlaying(0);
    if (busy > _pending_count) { show_error("播放缓冲状态异常"); return; }
    while (_pending_count > busy) {
        _played_samples += _play_lengths[pop_pending()];
        _last_progress = GetHAL().millis();
    }
    if (_pending_count < 2 && _reader.remainingSamples() && GetHAL().speaker.canPlay(0)) {
        const size_t index = free_buffer();
        if (index == BUFFER_COUNT) { show_error("播放缓冲状态异常"); return; }
        auto* data = _pcm + index * BLOCK_SAMPLES;
        const size_t samples = _reader.read(data, BLOCK_SAMPLES);
        if (_reader.failed() || !samples) {
            mclog::tagError(getAppInfo().name, "read: {}", _reader.error());
            show_error("录音读取中断，请检查SD卡"); return;
        }
        update_wave(data, samples);
        if (!GetHAL().speaker.playRaw(data, samples, _reader.sampleRate(), false, 1, 0)) {
            show_error("录音播放失败"); return;
        }
        _pending[_pending_count++] = index;
        _play_lengths[index] = samples;
        _last_progress = GetHAL().millis();
    }
    if (!_reader.remainingSamples() && !_pending_count) { log_stage("sd_play_complete"); stop_playback(); return; }
    const uint32_t now = GetHAL().millis();
    if (now - _last_progress >= STALL_TIMEOUT_MS) { show_error("播放停止响应，请重试"); return; }
    if (now - _last_draw >= 100) { _last_draw = now; render_audio(); }
}
void AppRecord::stop_playback()
{
    GetHAL().speaker.stop();
    GetHAL().speaker.end();
    _reader.close();
    _pending_count = 0;
    _phase = Phase::Library;
    render_library();
}
void AppRecord::update_wave(const int16_t* data, size_t samples)
{
    for (size_t i = 0; i < _wave.size(); ++i) _wave[i] = data[i * samples / _wave.size()];
    for (size_t i = 0; i < samples; ++i) {
        const int32_t value = data[i];
        _peak = std::max(_peak, value < 0 ? -value : value);
    }
}
void AppRecord::render_library()
{
    auto& c = GetHAL().canvas;
    c.fillScreen(THEME_COLOR_BG);
    c.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    c.setCursor(3, 0); c.print("SD卡录音 · 最新64条");
    c.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    if (_entries.empty()) { c.setCursor(3, 30); c.print("暂无录音，按OK开始"); }
    const size_t start = (_selected / 4) * 4;
    for (size_t i = start; i < _entries.size() && i < start + 4; ++i) {
        c.setCursor(3, 18 + (i - start) * 17);
        c.setTextColor(i == _selected ? TFT_ORANGE : TFT_WHITE, THEME_COLOR_BG);
        const auto& e = _entries[i];
        const auto line = std::string(i == _selected ? "> " : "  ") + e.name + "  " + std::to_string(e.dataBytes / (2 * e.sampleRate)) + "s";
        c.print(line.c_str());
    }
    c.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    c.setCursor(3, c.height() - 26); c.print("OK回放  N新录音");
    c.setCursor(3, c.height() - 13); c.print("上下选择  G0返回");
    GetHAL().pushCanvas();
}
void AppRecord::render_audio()
{
    auto& c = GetHAL().canvas;
    c.fillScreen(THEME_COLOR_BG);
    c.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    c.setCursor(3, 0);
    c.print(_phase == Phase::Recording ? "正在录音  OK停止保存" : _phase == Phase::Stopping ? "正在保存，请稍候…" : "正在回放  OK停止");
    const std::string path = _phase == Phase::Playing ? _entries[_selected].name : _writer.path().substr(_writer.path().find_last_of('/') + 1);
    c.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    c.setCursor(3, 16); c.print(path.c_str());
    c.setCursor(3, 32);
    const uint32_t seconds = _phase == Phase::Playing ? _played_samples / _reader.sampleRate() : _writer.dataBytes() / (2 * SAMPLE_RATE);
    c.print((std::to_string(seconds) + " 秒").c_str());
    const int top = 48, bottom = c.height() - 18;
    const int mid = (top + bottom) / 2;
    for (size_t x = 0; x < _wave.size() && x + 3 < static_cast<size_t>(c.width()); ++x) {
        const int y = std::max(top, std::min(bottom, mid + _wave[x] / 1800));
        c.drawPixel(x + 3, y, TFT_WHITE);
    }
    c.setCursor(3, c.height() - 13); c.print("G0保存并返回");
    if (_phase == Phase::Playing) { c.fillRect(0, c.height() - 14, c.width(), 14, THEME_COLOR_BG); c.setCursor(3, c.height() - 13); c.print("G0返回菜单"); }
    GetHAL().pushCanvas();
}
void AppRecord::show_error(const char* message)
{
    // Cancel asynchronous owners before saving partial data or reusing buffers.
    GetHAL().mic.end();
    GetHAL().speaker.stop();
    GetHAL().speaker.end();
    _reader.close();
    if (_writer.active() && !_writer.finish()) mclog::tagError(getAppInfo().name, "partial save: {}", _writer.error());
    _pending_count = 0;
    _phase = Phase::Error;
    log_stage("sd_error");
    mclog::tagError(getAppInfo().name, "{}", message);
    auto& c = GetHAL().canvas;
    c.fillScreen(THEME_COLOR_BG);
    c.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    c.setCursor(3, 16); c.print(message);
    c.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    c.setCursor(3, 46); c.print("插好FAT32卡后按OK重试");
    c.setCursor(3, 65); c.print("G0返回，不会格式化卡");
    GetHAL().pushCanvas();
}
