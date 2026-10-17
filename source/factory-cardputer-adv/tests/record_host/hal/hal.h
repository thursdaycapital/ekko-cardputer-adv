#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

constexpr int KEY_ENTER = 13;
constexpr int KEY_N = 'n';
constexpr int KEY_UP = 1001;
constexpr int KEY_DOWN = 1002;
constexpr int KEY_SEMICOLON = ';';
constexpr int KEY_DOT = '.';
constexpr uint32_t TFT_ORANGE = 0xffa500;
constexpr uint32_t TFT_WHITE = 0xffffff;

namespace record_test {
inline std::vector<std::string> events;
inline bool keyboard_sfx = true;
inline bool fail_record_allocation = false;
inline size_t pcm_allocation_bytes = 0;
inline unsigned heap_free = 160000;
inline unsigned heap_largest = 80000;
}

namespace m5 {
struct mic_config_t {
    int pin_data_in = 46;
    int pin_bck = 41;
    int pin_ws = 43;
    uint32_t sample_rate = 16000;
    uint8_t over_sampling = 1;
    uint8_t magnification = 16;
    uint8_t noise_filter_level = 0;
    bool stereo = false;
};
}

struct FakeMic {
    struct Request { int16_t* data; size_t length; };
    m5::mic_config_t configuration;
    bool running = false;
    bool fail_begin = false;
    bool fail_record = false;
    bool allow_record = true;
    bool task_started = true;
    size_t maximum_pending = 0;
    size_t record_calls = 0;
    size_t begin_calls = 0;
    int next_sample = 0;
    std::vector<Request> pending;
    std::vector<Request> submitted;

    m5::mic_config_t config() const { return configuration; }
    void config(const m5::mic_config_t& value) { configuration = value; }
    bool isEnabled() const { return configuration.pin_data_in >= 0; }
    bool isRunning() const { return running; }
    bool begin() {
        ++begin_calls;
        record_test::events.emplace_back("mic.begin");
        running = !fail_begin && isEnabled();
        return running;
    }
    void end() {
        record_test::events.emplace_back("mic.end");
        // A genuine asynchronous reader may still own the recording buffer.
        // Access it while stopping so ASan detects release-before-end mistakes.
        for (const auto& request : pending) {
            if (request.length) {
                volatile int16_t* reader = request.data;
                *reader = *reader;
            }
        }
        pending.clear();
        running = false;
    }
    size_t isRecording() const { return task_started ? pending.size() : 0; }
    size_t recordingQueueSize() const { return pending.size(); }
    bool canRecord() const { return running && allow_record && pending.size() < 2; }
    bool record(int16_t* data, size_t length, uint32_t rate, bool stereo = false) {
        ++record_calls;
        if (!running || fail_record || !data || !length || rate != 16000 || stereo) return false;
        if (!canRecord()) throw std::runtime_error("record() called without a writable SDK slot");
        for (const auto& request : pending) {
            if (data < request.data + request.length && request.data < data + length)
                throw std::runtime_error("microphone reuses a buffer still owned by I2S");
        }
        pending.push_back({data, length});
        maximum_pending = std::max(maximum_pending, pending.size());
        submitted.push_back({data, length});
        return true;
    }
    void completeOne() {
        if (pending.empty()) return;
        const auto request = pending.front();
        pending.erase(pending.begin());
        for (size_t i = 0; i < request.length; ++i) {
            request.data[i] = static_cast<int16_t>(next_sample++ % 30000);
        }
    }
    void completeAll() { while (!pending.empty()) completeOne(); }
};

struct FakeSpeaker {
    struct Request {
        const int16_t* data;
        size_t length;
        uint32_t rate;
        int channel;
        std::vector<int16_t> original;
    };
    bool running = true;
    bool fail_begin = false;
    bool fail_play = false;
    bool allow_play = true;
    size_t maximum_pending = 0;
    uint8_t volume = 73;
    size_t begin_calls = 0;
    std::vector<Request> pending;
    std::vector<Request> submitted;
    std::vector<int16_t> heard;

    bool isEnabled() const { return true; }
    bool isRunning() const { return running; }
    bool begin() {
        ++begin_calls;
        record_test::events.emplace_back("speaker.begin");
        running = !fail_begin;
        return running;
    }
    void stop() {
        record_test::events.emplace_back("speaker.stop");
        touchPending();
        pending.clear();
    }
    void stop(uint8_t) { stop(); }
    void end() {
        record_test::events.emplace_back("speaker.end");
        touchPending();
        pending.clear();
        running = false;
    }
    void touchPending() {
        for (const auto& request : pending) {
            if (request.length) {
                if (!std::equal(request.original.begin(), request.original.end(), request.data))
                    throw std::runtime_error("PCM changed before speaker cancellation completed");
                volatile int16_t last = request.data[request.length - 1];
                (void)last;
            }
        }
    }
    uint8_t getVolume() const { return volume; }
    void setVolume(uint8_t value) { volume = value; }
    bool isPlaying() const { return !pending.empty(); }
    bool canPlay(uint8_t channel) const { return running && allow_play && isPlaying(channel) < 2; }
    size_t isPlaying(uint8_t channel) const {
        return std::count_if(pending.begin(), pending.end(), [channel](const auto& r) { return r.channel == channel; });
    }
    bool playRaw(const int16_t* data, size_t length, uint32_t rate = 44100,
                 bool stereo = false, uint32_t repeat = 1, int channel = -1,
                 bool stop_current_sound = false) {
        if (!running || fail_play || !data || !length || rate != 16000 || stereo || repeat != 1) return false;
        if (stop_current_sound) pending.clear();
        if (channel != 0 || !canPlay(channel)) throw std::runtime_error("playRaw() called without a writable channel slot");
        for (const auto& pending_request : pending) {
            if (data < pending_request.data + pending_request.length && pending_request.data < data + length)
                throw std::runtime_error("speaker reuses a buffer before completion");
        }
        Request request{data, length, rate, channel, std::vector<int16_t>(data, data + length)};
        pending.push_back(request);
        maximum_pending = std::max(maximum_pending, pending.size());
        submitted.push_back(request);
        record_test::events.emplace_back("speaker.play");
        return true;
    }
    void completeOne() {
        if (pending.empty()) return;
        const auto request = pending.front();
        if (!std::equal(request.original.begin(), request.original.end(), request.data))
            throw std::runtime_error("PCM changed while speaker still owns it");
        heard.insert(heard.end(), request.data, request.data + request.length);
        pending.erase(pending.begin());
    }
    void completeAll() { while (!pending.empty()) completeOne(); }
};

struct FakeCanvas {
    std::string text;
    int width() const { return 204; }
    int height() const { return 109; }
    void fillScreen(uint32_t) { text.clear(); }
    void setTextColor(uint32_t, uint32_t = 0) {}
    void setCursor(int, int) {}
    void setTextSize(int) {}
    void setTextScroll(bool) {}
    template<class T> void setFont(T) {}
    void fillRect(int, int, int, int, uint32_t) {}
    void drawPixel(int, int, uint32_t) {}
    void print(const char* value) { text += value; }
    void print(const std::string& value) { text += value; }
    void println(const char* value) { text += value; text += '\n'; }
};

struct FakeKeyboard {
    struct KeyEvent_t { bool state = false; int keyCode = 0; };
    KeyEvent_t next;
    KeyEvent_t getLatestKeyEvent() { const auto value = next; next = {}; return value; }
    void enter() { next = {true, KEY_ENTER}; }
    void press(int key) { next = {true, key}; }
};

struct FakeButton {
    bool clicked = false;
    bool wasClicked() { const bool value = clicked; clicked = false; return value; }
};

struct FakeHal {
    FakeMic mic;
    FakeSpeaker speaker;
    FakeCanvas canvas;
    FakeKeyboard keyboard;
    FakeButton homeButton;
    bool sd_mounted = true;
    std::string sd_mount_point;
    size_t mount_calls = 0;
    bool ensureSdCardMounted() { ++mount_calls; return sd_mounted; }
    const char* sdCardMountPoint() const { return sd_mount_point.c_str(); }
    uint32_t now = 0;
    size_t pushes = 0;
    size_t delay_calls = 0;
    uint32_t millis() const { return now; }
    void delay(uint32_t ms) {
        now += ms;
        if (++delay_calls > 100) throw std::runtime_error("blocking wait in AppRecord lifecycle");
    }
    void pushCanvas() { ++pushes; }
};

FakeHal& GetHAL();
