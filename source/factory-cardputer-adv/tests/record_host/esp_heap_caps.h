#pragma once
#include <hal/hal.h>
constexpr unsigned MALLOC_CAP_INTERNAL = 1;
constexpr unsigned MALLOC_CAP_8BIT = 2;
inline size_t heap_caps_get_free_size(unsigned) { return record_test::heap_free; }
inline size_t heap_caps_get_largest_free_block(unsigned) { return record_test::heap_largest; }
inline size_t heap_caps_get_minimum_free_size(unsigned) { return record_test::heap_free; }
