#include <hal/keyboard/keyboard.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <deque>
#include <string>

namespace {
std::array<uint8_t, 256> registers{};
std::deque<uint8_t> fifo;
void (*interrupt_handler)(void*) = nullptr;
void* interrupt_argument = nullptr;
unsigned event_reads = 0, irq_reads = 0, irq_writes = 0, available_reads = 0;
unsigned fail_event_reads = 0, fail_status_reads = 0;
unsigned raw_signals = 0, key_signals = 0;
Keyboard::KeyEventRaw_t last_raw;
Keyboard::KeyEvent_t last_key;

void interrupt()
{
    assert(interrupt_handler);
    interrupt_handler(interrupt_argument);
}
void enqueue(uint8_t value, bool trigger = true)
{
    fifo.push_back(value);
    registers[TCA8418_REG_INT_STAT] |= 1;
    if (trigger) interrupt();
}
uint8_t physical_code(uint8_t row, uint8_t col, bool down)
{
    assert(row < 4 && col < 14);
    // Inverse board wiring: seven pairs of display columns, eight scan columns.
    const uint8_t scan_row = col / 2;
    const uint8_t scan_column = row + (col % 2) * 4;
    return static_cast<uint8_t>(scan_row * 10 + scan_column + 1 + (down ? 128 : 0));
}
void press(Keyboard& keyboard, uint8_t row, uint8_t col, bool down)
{
    enqueue(physical_code(row, col, down));
    keyboard.update();
    assert(last_raw.row == row && last_raw.col == col && last_raw.state == down);
}
void release_modifiers(Keyboard& keyboard)
{
    press(keyboard, 2, 0, false); // Fn
    press(keyboard, 2, 1, false); // Aa / Shift
    press(keyboard, 3, 0, false); // Ctrl
    press(keyboard, 3, 1, false); // Opt / Meta
    press(keyboard, 3, 2, false); // Alt
    assert(keyboard.getModifierMask() == 0);
    keyboard.clearKeyEvent();
}
void assert_empty(const Keyboard::KeyEvent_t& key)
{
    assert(!key.state && !key.isModifier && key.keyCode == KEY_NONE);
    assert(key.keyName && std::string(key.keyName).empty() && key.extraModifiers == 0);
}
void assert_clear(Keyboard& keyboard)
{
    assert_empty(keyboard.getLatestKeyEvent());
    const auto& raw = keyboard.getLatestKeyEventRaw();
    assert(!raw.state && raw.row == UINT8_MAX && raw.col == UINT8_MAX);
}
bool valid_matrix_event(uint8_t value)
{
    const unsigned number = value & 127;
    // TCA8418 code = row * 10 + column + 1, only 7 x 8 pins are keys.
    return number >= 1 && number <= 68 && number % 10 >= 1 && number % 10 <= 8;
}
const KeScanCode_t normal_codes[4][14] = {
    {KEY_GRAVE, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
     KEY_0, KEY_MINUS, KEY_EQUAL, KEY_BACKSPACE},
    {KEY_TAB, KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O,
     KEY_P, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_BACKSLASH},
    {KEY_NONE, KEY_LEFTSHIFT, KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J,
     KEY_K, KEY_L, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_ENTER},
    {KEY_LEFTCTRL, KEY_LEFTMETA, KEY_LEFTALT, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B,
     KEY_N, KEY_M, KEY_COMMA, KEY_DOT, KEY_SLASH, KEY_SPACE},
};
void test_all_raw_values(Keyboard& keyboard)
{
    unsigned valid = 0, invalid = 0;
    for (unsigned number = 0; number < 256; ++number) {
        release_modifiers(keyboard);
        const auto value = static_cast<uint8_t>(number);
        const auto raw_before = raw_signals;
        const auto key_before = key_signals;
        const auto writes_before = irq_writes;
        enqueue(value);
        keyboard.update();
        assert(irq_writes == writes_before + 1); // Invalid input still acknowledges IRQ.
        if (valid_matrix_event(value)) {
            ++valid;
            assert(raw_signals == raw_before + 1 && key_signals == key_before + 1);
            const unsigned index = (number & 127) - 1;
            const unsigned scan_row = index / 10;
            const unsigned scan_column = index % 10;
            const unsigned expected_row = scan_column % 4;
            const unsigned expected_col = scan_row * 2 + scan_column / 4;
            assert(last_raw.row == expected_row && last_raw.col == expected_col);
            assert(last_raw.state == static_cast<bool>(number & 128));
            assert(last_key.state == last_raw.state);
            assert(last_key.keyCode == normal_codes[expected_row][expected_col]);
            assert(last_key.keyName && last_key.keyName[0]);
            assert(last_key.extraModifiers == 0);
        } else {
            ++invalid;
            assert(raw_signals == raw_before && key_signals == key_before);
            assert(keyboard.getModifierMask() == 0);
            assert_clear(keyboard);
        }
        assert(fifo.empty());
    }
    assert(valid == 112 && invalid == 144);
    release_modifiers(keyboard);
    puts("PASS: all 256 raw values; 112 legal press/release mappings; 144 rejected values");
}
void test_invalid_events_preserve_modifiers(Keyboard& keyboard)
{
    press(keyboard, 2, 0, true);
    press(keyboard, 2, 1, true);
    press(keyboard, 3, 0, true);
    press(keyboard, 3, 1, true);
    press(keyboard, 3, 2, true);
    constexpr uint8_t held = KEY_MOD_LCTRL | KEY_MOD_LSHIFT | KEY_MOD_LALT | KEY_MOD_LMETA;
    assert(keyboard.getModifierMask() == held);
    for (unsigned number = 0; number < 256; ++number) {
        const auto value = static_cast<uint8_t>(number);
        if (valid_matrix_event(value)) continue;
        const auto raw_before = raw_signals, key_before = key_signals;
        enqueue(value);
        keyboard.update();
        assert(raw_signals == raw_before && key_signals == key_before);
        assert(keyboard.getModifierMask() == held);
        assert_clear(keyboard);
        const auto arrow = keyboard.convertToKeyEvent({true, 2, 11});
        assert(arrow.keyCode == KEY_UP); // Invalid data did not release Fn.
    }
    release_modifiers(keyboard);
    puts("PASS: invalid inputs do not emit normal/modifier events or change held modifiers/Fn");
}
void test_empty_and_i2c_retry_fifo(Keyboard& keyboard)
{
    release_modifiers(keyboard);
    const auto signals_before = key_signals;
    const auto events_before = event_reads;
    interrupt(); // Stale/spurious IRQ while FIFO is empty.
    keyboard.update();
    assert(key_signals == signals_before && event_reads == events_before + 1);
    assert_clear(keyboard);
    keyboard.update();
    assert(event_reads == events_before + 1); // Successful clear stopped empty polling.

    // Invalid item must not discard a following valid item in the same IRQ batch.
    enqueue(0xff);
    enqueue(physical_code(2, 13, true), false);
    keyboard.update();
    assert(key_signals == signals_before);
    keyboard.update();
    assert(key_signals == signals_before + 1 && last_key.keyCode == KEY_ENTER && last_key.state);

    // An event-register read failure returns zero without consuming the FIFO.
    enqueue(physical_code(1, 1, true));
    fail_event_reads = 1;
    const auto current_signals = key_signals;
    keyboard.update();
    assert(key_signals == current_signals && fifo.size() == 1);
    assert_clear(keyboard);
    keyboard.update();
    assert(key_signals == current_signals + 1 && last_key.keyCode == KEY_Q && fifo.empty());

    // If status read also fails, preserve software IRQ and retry with no new edge.
    enqueue(physical_code(3, 12, true));
    fail_event_reads = 1;
    fail_status_reads = 1;
    keyboard.update();
    assert_clear(keyboard);
    assert(fifo.size() == 1);
    keyboard.update();
    assert(last_key.keyCode == KEY_SLASH && last_key.state && fifo.empty());

    // A failed status read after the final event causes one harmless empty read.
    enqueue(physical_code(3, 12, false));
    fail_status_reads = 1;
    keyboard.update();
    const auto count_after_release = key_signals;
    keyboard.update();
    assert_clear(keyboard);
    assert(key_signals == count_after_release);
    const auto event_after_empty = event_reads;
    keyboard.update();
    assert(event_reads == event_after_empty);
    assert(available_reads == 0); // No extra available() read gates FIFO processing.
    release_modifiers(keyboard);
    puts("PASS: empty FIFO, invalid-before-valid FIFO, I2C event/status failure retries and IRQ clear");
}
void test_modifier_layers_and_clear(Keyboard& keyboard)
{
    release_modifiers(keyboard);
    press(keyboard, 2, 1, true); // Shift.
    assert(last_key.isModifier && keyboard.getModifierMask() == KEY_MOD_LSHIFT);
    keyboard.clearKeyEvent();
    assert_clear(keyboard);
    assert(keyboard.getModifierMask() == KEY_MOD_LSHIFT);
    press(keyboard, 2, 2, true);
    assert(last_key.keyCode == KEY_A && std::string(last_key.keyName) == "A");
    assert(!last_key.isModifier && last_key.extraModifiers == 0);
    press(keyboard, 0, 1, true);
    assert(std::string(last_key.keyName) == "!");
    press(keyboard, 2, 1, false);
    press(keyboard, 2, 2, true);
    assert(std::string(last_key.keyName) == "a" && keyboard.getModifierMask() == 0);

    press(keyboard, 2, 0, true); // Fn remains separate from HID modifier mask.
    assert(last_key.isModifier && last_key.keyCode == KEY_NONE && std::string(last_key.keyName) == "fn");
    assert(keyboard.getModifierMask() == 0);
    keyboard.clearKeyEvent();
    assert_clear(keyboard);
    press(keyboard, 2, 2, true);
    assert(last_key.keyCode == KEY_A && std::string(last_key.keyName) == "A");
    assert(last_key.extraModifiers == KEY_MOD_LSHIFT);
    keyboard.update();
    assert_clear(keyboard); // Automatic next-frame clearing includes stale state/name/extraModifiers.
    press(keyboard, 0, 0, true);
    assert(last_key.keyCode == KEY_ESC && std::string(last_key.keyName) == "esc");
    press(keyboard, 0, 13, true);
    assert(last_key.keyCode == KEY_DELETE);
    press(keyboard, 2, 11, true);
    assert(last_key.keyCode == KEY_UP);
    press(keyboard, 3, 10, true);
    assert(last_key.keyCode == KEY_LEFT);
    press(keyboard, 3, 11, true);
    assert(last_key.keyCode == KEY_DOWN);
    press(keyboard, 3, 12, true);
    assert(last_key.keyCode == KEY_RIGHT);
    press(keyboard, 1, 0, true);
    assert(last_key.keyCode == KEY_TAB && last_key.extraModifiers == 0);
    press(keyboard, 3, 13, true);
    assert(last_key.keyCode == KEY_SPACE && last_key.extraModifiers == 0);
    press(keyboard, 2, 0, false);
    press(keyboard, 2, 11, true);
    assert(last_key.keyCode == KEY_SEMICOLON);
    press(keyboard, 3, 12, true);
    assert(last_key.keyCode == KEY_SLASH);

    press(keyboard, 3, 0, true);
    press(keyboard, 3, 13, true);
    assert(last_key.keyCode == KEY_SPACE && keyboard.getModifierMask() == KEY_MOD_LCTRL);
    press(keyboard, 2, 0, true);
    press(keyboard, 2, 2, true);
    assert(keyboard.getModifierMask() == KEY_MOD_LCTRL && last_key.extraModifiers == KEY_MOD_LSHIFT);
    release_modifiers(keyboard);
    puts("PASS: Shift/Aa, Fn letters/arrows/Esc/Delete, normal punctuation, Ctrl+Space and complete event clearing");
}
void test_public_conversion_bounds(Keyboard& keyboard)
{
    release_modifiers(keyboard);
    for (unsigned row = 0; row < 256; ++row)
        for (unsigned col = 0; col < 256; ++col) {
            const auto key = keyboard.convertToKeyEvent({true, static_cast<uint8_t>(row), static_cast<uint8_t>(col)});
            if (row >= 4 || col >= 14) assert_empty(key);
            else assert(key.state && key.keyCode == normal_codes[row][col]);
        }
    puts("PASS: public converter bounds across all 65,536 row/column pairs");
}
}
namespace keyboard_host {
bool read(uint8_t reg, uint8_t* data, std::size_t length)
{
    assert(length == 1 && data);
    if (reg == TCA8418_REG_KEY_EVENT_A) {
        ++event_reads;
        if (fail_event_reads) { --fail_event_reads; return false; }
        *data = fifo.empty() ? 0 : fifo.front();
        if (!fifo.empty()) fifo.pop_front();
    } else if (reg == TCA8418_REG_INT_STAT) {
        ++irq_reads;
        if (fail_status_reads) { --fail_status_reads; return false; }
        *data = registers[reg];
    } else {
        if (reg == TCA8418_REG_KEY_LCK_EC) ++available_reads;
        *data = registers[reg];
    }
    return true;
}
bool write(uint8_t reg, uint8_t value)
{
    if (reg == TCA8418_REG_INT_STAT) {
        ++irq_writes;
        registers[reg] &= ~value;
        if (!fifo.empty()) registers[reg] |= 1;
    } else registers[reg] = value;
    return true;
}
}
int gpio_config(const gpio_config_t* config)
{
    assert(config && config->pin_bit_mask == (1ULL << 11));
    assert(config->mode == GPIO_MODE_INPUT && config->intr_type == GPIO_INTR_ANYEDGE);
    return 0;
}
int gpio_install_isr_service(int flags) { assert(flags == ESP_INTR_FLAG_IRAM); return 0; }
int gpio_isr_handler_add(gpio_num_t pin, void (*handler)(void*), void* argument)
{
    assert(pin == 11 && handler);
    interrupt_handler = handler;
    interrupt_argument = argument;
    return 0;
}
int main()
{
    // The production keyboard lives for the lifetime of the HAL singleton.
    static Keyboard keyboard;
    keyboard.update();
    assert_clear(keyboard);
    assert(keyboard.init());
    assert(registers[TCA8418_REG_KP_GPIO_1] == 0x7f);
    assert(registers[TCA8418_REG_KP_GPIO_2] == 0xff);
    keyboard.onKeyEventRaw.connect([](const auto& value) { ++raw_signals; last_raw = value; });
    keyboard.onKeyEvent.connect([](const auto& value) { ++key_signals; last_key = value; });
    test_all_raw_values(keyboard);
    test_invalid_events_preserve_modifiers(keyboard);
    test_empty_and_i2c_retry_fifo(keyboard);
    test_modifier_layers_and_clear(keyboard);
    test_public_conversion_bounds(keyboard);
    puts("PASS: actual keyboard.cpp + actual Adafruit_TCA8418.cpp; ASan/UBSan");
}
