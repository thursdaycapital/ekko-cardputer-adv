/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <new>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <random>
#include "audio.h"

namespace audio {

static bool quiet_mode_is_enabled = false;

static std::vector<int> c_major_scale = {60, 62, 64, 65, 67, 69, 71};  // C大调音阶（C D E F G A B）

namespace {
// These UI helpers have a single producer (the application/keyboard task).
// Each channel holds at most one cue, so zero queued descriptors proves that
// its PCM can be replaced. Do not reuse a buffer merely because canPlay() is
// true: that also permits a second descriptor while the first still reads it.
constexpr size_t MAX_CUE_SAMPLES = 16384;
constexpr uint32_t LEGACY_CUE_RATE = 44100;
struct CueBuffer {
    std::unique_ptr<int16_t[]> data;
    size_t capacity = 0;
    uint8_t channel;
};
std::array<CueBuffer, 2> cue_buffers = {{{nullptr, 0, 7}, {nullptr, 0, 6}}};

void release_idle_buffers()
{
    for (auto& slot : cue_buffers) {
        if (GetHAL().speaker.isPlaying(slot.channel) == 0) {
            slot.data.reset();
            slot.capacity = 0;
        }
    }
}

CueBuffer* acquire_cue(size_t samples)
{
    if (!samples || samples > MAX_CUE_SAMPLES) return nullptr;
    for (auto& slot : cue_buffers) {
        if (GetHAL().speaker.isPlaying(slot.channel) || !GetHAL().speaker.canPlay(slot.channel)) continue;
        if (slot.capacity < samples) {
            // At most 32 KiB across both slots, including resize operations.
            slot.data.reset();
            slot.capacity = 0;
            size_t retained = 0;
            for (const auto& other : cue_buffers) retained += other.capacity;
            if (retained > MAX_CUE_SAMPLES - samples) {
                for (auto& other : cue_buffers) {
                    if (&other != &slot && GetHAL().speaker.isPlaying(other.channel) == 0) {
                        retained -= other.capacity;
                        other.data.reset();
                        other.capacity = 0;
                    }
                }
            }
            if (retained > MAX_CUE_SAMPLES - samples) continue;
            slot.data.reset(new (std::nothrow) int16_t[samples]);
            if (!slot.data) return nullptr;
            slot.capacity = samples;
        }
        return &slot;
    }
    // A dense burst drops new cues instead of blocking the UI or growing RAM.
    return nullptr;
}

bool cue_length(uint32_t sample_rate, double duration, size_t notes, size_t& samples_per_note)
{
    if (!sample_rate || !notes || !std::isfinite(duration) || duration <= 0) return false;
    const double frames = sample_rate * duration;
    if (!std::isfinite(frames) || frames < 1 || frames > double(MAX_CUE_SAMPLES / 2)) return false;
    samples_per_note = static_cast<size_t>(frames);
    return notes <= MAX_CUE_SAMPLES / 2 / samples_per_note;
}

bool midi_frequency(int midi, double& frequency)
{
    if (midi < 0 || midi > 127) return false;
    frequency = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
    return std::isfinite(frequency) && frequency <= std::numeric_limits<int>::max();
}

void submit_cue(CueBuffer& slot, size_t samples)
{
    // Preserve the factory's actual waveform and audible pitch/duration. Its
    // duplicated L/R values historically play as paired mono at 44100 Hz,
    // regardless of the synthesis rate. Correcting that to stereo would make
    // existing key sounds substantially higher and shorter.
    GetHAL().speaker.playRaw(slot.data.get(), samples, LEGACY_CUE_RATE, false, 1, slot.channel, false);
}
} // namespace

void play_tone(int frequency, double durationSec)
{
    if (GetHAL().speaker.getVolume() <= 0) {
        return;
    }

    const uint32_t sample_rate = GetHAL().speaker.config().sample_rate;
    size_t samples = 0;
    if (frequency <= 0 || !cue_length(sample_rate, durationSec, 1, samples)) return;
    auto* slot = acquire_cue(samples * 2);
    if (!slot) return;
    auto* buffer = slot->data.get();

    const size_t fade_len = 200;  // 淡出长度（采样点）
    const float amplitude = 32767.0f / 5;

    for (size_t i = 0; i < samples; ++i) {
        float amp = amplitude;

        // 应用结尾淡出（fade-out）
        if (samples - i <= fade_len) {
            float fade_factor = static_cast<float>(samples - i) / fade_len;
            amp *= fade_factor;
        }

        int16_t value     = static_cast<int16_t>(amp * sin(2.0 * M_PI * frequency * i / sample_rate));
        buffer[i * 2]     = value;  // 左声道
        buffer[i * 2 + 1] = value;  // 右声道
    }

    submit_cue(*slot, samples * 2);
}

void play_melody(const std::vector<int>& midiList, double durationSec)
{
    if (GetHAL().speaker.getVolume() <= 0) {
        return;
    }

    const uint32_t sample_rate = GetHAL().speaker.config().sample_rate;
    size_t samples_per_note = 0;
    if (!cue_length(sample_rate, durationSec, midiList.size(), samples_per_note)) return;
    for (int midi : midiList) { double frequency; if (midi >= 0 && !midi_frequency(midi, frequency)) return; }
    const size_t total_samples = samples_per_note * midiList.size() * 2;
    auto* slot = acquire_cue(total_samples);
    if (!slot) return;
    auto* buffer = slot->data.get();
    size_t cursor = 0;
    const size_t fade_len = 200;  // 每个音符结尾的淡出长度
    const float amplitude      = 32767.0f / 5;

    for (int midiNote : midiList) {
        for (size_t i = 0; i < samples_per_note; ++i) {
            float amp = amplitude;

            // 应用淡出（仅每个音符的结尾）
            if (samples_per_note - i <= fade_len) {
                float fade_factor = static_cast<float>(samples_per_note - i) / fade_len;
                amp *= fade_factor;
            }

            int16_t sample = 0;
            if (midiNote >= 0) {
                double freq = 440.0 * pow(2.0, (midiNote - 69) / 12.0);
                sample      = static_cast<int16_t>(amp * sin(2.0 * M_PI * freq * i / sample_rate));
            }

            buffer[cursor++] = sample;  // Legacy paired mono waveform.
            buffer[cursor++] = sample;
        }
    }

    submit_cue(*slot, total_samples);
}

void play_tone_from_midi(int midi, double durationSec)
{
    if (GetHAL().speaker.getVolume() <= 0) {
        return;
    }

    double freq = 0;
    if (!midi_frequency(midi, freq)) return;
    play_tone(static_cast<int>(freq), durationSec);
}

void play_random_tone(int semitoneShift, double durationSec)
{
    // No random sounds during quiet mode
    if (GetHAL().speaker.getVolume() <= 0 || is_quiet_mode()) {
        return;
    }

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, static_cast<int>(c_major_scale.size()) - 1);

    int index = dist(gen);
    const int64_t midi = int64_t(c_major_scale[index]) + semitoneShift;
    if (midi < 0 || midi > 127) return;

    play_tone_from_midi(static_cast<int>(midi), durationSec);
}

/* -------------------------------------------------------------------------- */
/*                                  Keyboard                                  */
/* -------------------------------------------------------------------------- */
static void _keyboard_sfx_on_key_event(const Keyboard::KeyEvent_t& event)
{
    // No keypress sounds during quiet mode
    if (!event.state || is_quiet_mode()) {
        return;
    }

    GetHAL().speaker.setVolume(DEFAULT_VOLUME);

    int semitoneShift = 48;
    switch (event.keyCode) {
        case KEY_1:
            play_tone_from_midi(c_major_scale[0] + semitoneShift, 0.02);
            return;
        case KEY_2:
            play_tone_from_midi(c_major_scale[1] + semitoneShift, 0.02);
            return;
        case KEY_3:
            play_tone_from_midi(c_major_scale[2] + semitoneShift, 0.02);
            return;
        case KEY_4:
            play_tone_from_midi(c_major_scale[3] + semitoneShift, 0.02);
            return;
        case KEY_5:
            play_tone_from_midi(c_major_scale[4] + semitoneShift, 0.02);
            return;
        case KEY_6:
            play_tone_from_midi(c_major_scale[5] + semitoneShift, 0.02);
            return;
        case KEY_7:
            play_tone_from_midi(c_major_scale[6] + semitoneShift, 0.02);
            return;
        default:
            play_random_tone(semitoneShift, 0.02);
    }
}

void set_keyboard_sfx_enable(bool enable)
{
    static size_t slot_id  = 0;
    static bool is_enabled = false;

    // In particular, reclaim completed/cancelled cues after Speaker.end().
    // Cues still being consumed remain owned even when callbacks are disabled.
    release_idle_buffers();
    mclog::tagInfo("audio", "set keyboard sfx enable: {}", enable);
    if (enable) {
        if (is_enabled) {
            return;
        }
        is_enabled = true;
        slot_id    = GetHAL().keyboard.onKeyEvent.connect(_keyboard_sfx_on_key_event);
    } else {
        if (!is_enabled) {
            return;
        }
        is_enabled = false;
        GetHAL().keyboard.onKeyEvent.disconnect(slot_id);
    }
}

bool is_quiet_mode()
{
    return quiet_mode_is_enabled;
}

void set_quiet_mode(bool quiet)
{
    quiet_mode_is_enabled = quiet;
}

}  // namespace audio
