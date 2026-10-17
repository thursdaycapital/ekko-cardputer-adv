#pragma once
#include <hal/hal.h>
namespace audio {
constexpr int DEFAULT_VOLUME = 90;
inline void set_keyboard_sfx_enable(bool enable) { record_test::keyboard_sfx = enable; }
inline bool is_quiet_mode() { return false; }
}
