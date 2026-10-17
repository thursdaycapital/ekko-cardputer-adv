#pragma once
typedef int gpio_num_t;
#define GPIO_NUM_NC (-1)
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) ((pin) >= 0 && (pin) < 49 && (pin) != 46)
