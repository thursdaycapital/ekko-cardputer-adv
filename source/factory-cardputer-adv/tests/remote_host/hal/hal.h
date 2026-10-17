#pragma once
#include <esp_err.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

constexpr int KEY_NONE = 0, KEY_ENTER = 13, KEY_TAB = 9, KEY_ESC = 27;
constexpr int KEY_LEFT = 1001, KEY_RIGHT = 1002;
constexpr int KEY_UP = 1003, KEY_DOWN = 1004, KEY_SEMICOLON = ';', KEY_DOT = '.';
constexpr int KEY_COMMA = ',', KEY_SLASH = '/', KEY_1 = '1', KEY_0 = '0';
constexpr uint32_t TFT_ORANGE = 0xffa500, TFT_WHITE = 0xffffff;
constexpr uint32_t TFT_RED = 0xff0000, TFT_GREEN = 0x00ff00;
struct FakeCanvas {
    struct Printed { int x; int y; int size; std::string text; };
    std::string text;
    std::vector<Printed> printed;
    int x = 0, y = 0, size = 1;
    int height() const { return 109; }
    void fillScreen(uint32_t) { text.clear(); printed.clear(); }
    void setTextColor(uint32_t, uint32_t = 0) {}
    void setCursor(int next_x, int next_y) {
        if (next_x < 0 || next_y < 0 || next_x >= 204 || next_y >= 109) throw std::runtime_error("UI cursor outside body");
        x = next_x; y = next_y;
    }
    void setTextSize(int value) { size = value; }
    void fillRect(int left, int top, int width, int height, uint32_t) {
        if (left < 0 || top < 0 || width < 0 || height < 0 || left + width > 204 || top + height > 109)
            throw std::runtime_error("UI fill outside body");
    }
    void setTextScroll(bool) {}
    template<class T> void setFont(T) {}
    void print(const char* value) { text += value; text += '\n'; printed.push_back({x, y, size, value}); }
    void println(const char* value) { print(value); }
};
struct FakeKeyboard {
    struct KeyEvent { bool state = false; int keyCode = 0; bool isModifier = false; };
    KeyEvent latest;
    std::vector<KeyEvent> pending;
    uint32_t modifiers = 0;
    size_t clear_calls = 0;
    const KeyEvent& getLatestKeyEvent() const { return latest; }
    void clearKeyEvent() { ++clear_calls; latest = {}; }
    void update() {
        // Production clears the latest code at the start of every HAL frame,
        // then reads at most one FIFO event. Reading the latest event does not
        // consume it. The hardened production clear resets all event fields.
        clearKeyEvent();
        if (pending.empty()) return;
        latest = pending.front();
        pending.erase(pending.begin());
    }
    uint32_t getModifierMask() const { return modifiers; }
    void press(int code, bool is_modifier = false) { pending.push_back({true, code, is_modifier}); }
    void release(int code) { pending.push_back({false, code, false}); }
};
struct FakeButton {
    bool clicked = false;
    bool wasClicked() { bool result = clicked; clicked = false; return result; }
};
struct FakeHal {
    FakeCanvas canvas;
    FakeKeyboard keyboard;
    FakeButton homeButton;
    uint32_t now = 0;
    size_t pushes = 0, init_calls = 0, poll_calls = 0, cancel_calls = 0;
    bool init_ok = true, nec_ok = true;
    esp_err_t submit_result = ESP_OK, poll_result = ESP_ERR_TIMEOUT, cancel_result = ESP_OK;
    std::vector<std::vector<uint16_t>> submitted;
    std::vector<std::pair<uint8_t, uint8_t>> nec;
    uint32_t millis() const { return now; }
    void update() { keyboard.update(); }
    void pushCanvas() { ++pushes; }
    bool irInit() { ++init_calls; return init_ok; }
    bool irSend(uint8_t address, uint8_t command) { nec.emplace_back(address, command); return nec_ok; }
    esp_err_t irSendPulses(const uint16_t* durations, size_t count) {
        submitted.emplace_back(durations, durations + count);
        return submit_result;
    }
    esp_err_t irPoll() { ++poll_calls; return poll_result; }
    esp_err_t irCancel() { ++cancel_calls; return cancel_result; }
};
FakeHal& GetHAL();
