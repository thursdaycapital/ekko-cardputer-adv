#pragma once
#include <hal/hal.h>
inline uint32_t esp_get_free_heap_size() { return record_test::heap_free; }
