#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <vector>

constexpr int KEY_1 = '1', KEY_2 = '2', KEY_3 = '3', KEY_4 = '4', KEY_5 = '5', KEY_6 = '6', KEY_7 = '7';
struct Keyboard {
    struct KeyEvent_t { bool state; int keyCode; };
    struct Signal {
        size_t next = 1;
        std::map<size_t, std::function<void(const KeyEvent_t&)>> slots;
        size_t connect(std::function<void(const KeyEvent_t&)> callback) {
            const size_t id = next++; slots[id] = std::move(callback); return id;
        }
        void disconnect(size_t id) { slots.erase(id); }
        void emit(const KeyEvent_t& event) { for (const auto& slot : slots) slot.second(event); }
    } onKeyEvent;
};
struct CueSpeaker {
    struct Config { uint32_t sample_rate = 48000; } configuration;
    struct Request {
        const int16_t* data;
        size_t samples;
        uint32_t rate;
        bool stereo;
        int channel;
        std::vector<int16_t> original;
    };
    uint8_t volume = 90;
    bool fail_play = false;
    bool allow_play = true;
    std::vector<Request> pending;
    size_t calls = 0;
    Config config() const { return configuration; }
    uint8_t getVolume() const { return volume; }
    void setVolume(uint8_t value) { volume = value; }
    size_t isPlaying(uint8_t channel) const {
        return std::count_if(pending.begin(), pending.end(), [channel](const auto& r) { return r.channel == channel; });
    }
    bool canPlay(uint8_t channel) const { return allow_play && isPlaying(channel) < 2; }
    bool playRaw(const int16_t* data, size_t samples, uint32_t rate = 44100, bool stereo = false,
                 uint32_t repeat = 1, int channel = -1, bool replace = false) {
        ++calls;
        if (fail_play) return false;
        assert(data && samples && repeat == 1 && !replace);
        if (channel < 0) {
            for (channel = 7; channel >= 0 && isPlaying(channel); --channel) {}
        }
        assert(channel >= 0 && channel < 8 && canPlay(channel));
        touchPending();
        pending.push_back({data, samples, rate, stereo, channel, std::vector<int16_t>(data, data + samples)});
        return true;
    }
    void touchPending() const {
        for (const auto& request : pending) {
            if (!std::equal(request.original.begin(), request.original.end(), request.data))
                throw std::runtime_error("PCM changed while an asynchronous speaker still owns it");
        }
    }
    Request consumeOne() {
        assert(!pending.empty());
        touchPending();
        const Request request = pending.front(); pending.erase(pending.begin()); return request;
    }
    void end() { touchPending(); pending.clear(); }
};
struct CueHal { CueSpeaker speaker; Keyboard keyboard; };
CueHal& GetHAL();
