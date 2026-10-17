#include "firmware_adpcm_under_test.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

static std::vector<uint8_t> readFile(const char* path) {
  std::ifstream file(path, std::ios::binary);
  assert(file);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
  assert(argc >= 3 && (argc - 1) % 2 == 0);
  size_t checkedSamples = 0;
  for (int fixture = 1; fixture < argc; fixture += 2) {
    const std::vector<uint8_t> encoded = readFile(argv[fixture]);
    const std::vector<uint8_t> expected = readFile(argv[fixture + 1]);
    assert(encoded.size() >= 20 && encoded[4] == 1 && encoded[5] == 1);
    assert(readLe32(encoded.data() + 8) == 24000);
    const uint32_t samples = readLe32(encoded.data() + 12);
    assert(expected.size() == samples * 2 && encoded.size() == 20 + samples / 2);
    if (!samples) continue;
    int predictor = static_cast<int16_t>(readLe16(encoded.data() + 16));
    int index = encoded[18];
    assert(predictor == static_cast<int16_t>(readLe16(expected.data())));
    for (uint32_t sample = 1; sample < samples; ++sample) {
      const uint32_t nibbleIndex = sample - 1;
      const uint8_t packed = encoded[20 + nibbleIndex / 2];
      const uint8_t nibble = nibbleIndex % 2 ? packed >> 4 : packed & 0x0F;
      const int16_t actual = decodeImaAdpcmNibble(nibble, &predictor, &index);
      assert(actual == static_cast<int16_t>(readLe16(expected.data() + sample * 2)));
      assert(index >= 0 && index <= 88);
    }
    checkedSamples += samples;
  }
  std::cout << "Production firmware decoder matches actual server encoder/decoder: "
            << (argc - 1) / 2 << " fixtures / " << checkedSamples << " samples\n";
}
