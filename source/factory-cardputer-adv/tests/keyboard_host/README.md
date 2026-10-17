# Keyboard host checks

`python3 tests/run_keyboard_tests.py` compiles the actual production
`keyboard.cpp`, its header/key map, `Adafruit_TCA8418.cpp`, and the production
Mooncake signal implementation.

Only I2C register access, GPIO interrupt setup, and log output are test doubles.
The fake preserves the real `readRegister8` behavior of returning zero after
an I2C read failure. Its checked `readRegister` reports failure separately.
FIFO reads and IRQ acknowledgements are counted; a failed event read does not
consume the pending item. The tests do not claim physical I2C or key validation.

The ASan/UBSan checks cover all 256 raw values, all 56 physical positions in
both press/release directions, held modifiers during invalid input, pending
FIFO items after rejected events/read failures, complete event clearing,
Fn/Shift/Ctrl combinations, and the public conversion API across all 65,536
row/column inputs. Invalid events must acknowledge the IRQ without emitting
either event signal. A successful IRQ clear stops empty polling.

LeakSanitizer is disabled on macOS because that runtime does not support it.
