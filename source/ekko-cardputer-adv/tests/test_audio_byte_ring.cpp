#include "AudioByteRing.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <deque>
#include <iostream>
#include <thread>
#include <vector>

static uint8_t payload(size_t index) {
  // Never zero: returning zero-filled but unproduced storage cannot pass.
  return static_cast<uint8_t>((index * 137 + index / 251) % 255 + 1);
}

static void invalidAndEmpty() {
  uint8_t storage[4] = {0, 0, 0, 0};
  AudioByteRing zero(storage, 0), missing(nullptr, 4);
  AudioByteRing oversized(storage, static_cast<size_t>(UINT32_MAX) / 2 + 1);
  AudioByteRing maximum(storage, static_cast<size_t>(UINT32_MAX) / 2);
  assert(maximum.valid() && maximum.capacity() == UINT32_MAX / 2);
  uint8_t output[4] = {0xAA, 0xAA, 0xAA, 0xAA};
  for (AudioByteRing* invalid : {&zero, &missing, &oversized}) {
    assert(!invalid->valid() && invalid->capacity() == 0);
    assert(invalid->available() == 0 && invalid->freeSpace() == 0);
    assert(invalid->read(output, sizeof(output)) == 0);
    assert(invalid->write(output, sizeof(output)) == 0);
  }
  AudioByteRing ring(storage, sizeof(storage));
  assert(ring.valid() && ring.available() == 0 && ring.freeSpace() == 4);
  assert(ring.read(output, sizeof(output)) == 0 && output[0] == 0xAA);
  assert(ring.write(nullptr, 4) == 0 && ring.read(nullptr, 4) == 0);
  assert(ring.write(output, 0) == 0 && ring.read(output, 0) == 0);
  assert(ring.available() == 0 && ring.freeSpace() == 4);
}

static void fullPartialAndWrap() {
  std::array<uint8_t, 7> storage{};
  AudioByteRing ring(storage.data(), storage.size());
  const uint8_t first[] = {1, 2, 3, 4, 5, 6, 7, 8};
  assert(ring.write(first, sizeof(first)) == 7);
  assert(ring.available() == 7 && ring.freeSpace() == 0);
  assert(ring.write(first, 1) == 0);
  uint8_t output[10];
  std::fill(output, output + sizeof(output), 0xAA);
  assert(ring.read(output, 5) == 5);
  for (size_t i = 0; i < 5; ++i) assert(output[i] == i + 1);
  assert(output[5] == 0xAA);
  const uint8_t second[] = {8, 9, 10, 11, 12, 13};
  assert(ring.write(second, sizeof(second)) == 5);
  assert(ring.read(output, sizeof(output)) == 7);
  for (size_t i = 0; i < 7; ++i) assert(output[i] == i + 6);
  assert(output[7] == 0xAA && output[9] == 0xAA);
  assert(ring.available() == 0 && ring.freeSpace() == 7);
}

static uint32_t nextRandom(uint32_t& state) {
  state ^= state << 13; state ^= state >> 17; state ^= state << 5;
  return state;
}

static void sequentialReference(size_t capacity) {
  // Independent FIFO specification, including binary zero bytes and requests
  // larger than the ring. Guards verify read() never fills its unreturned tail.
  std::vector<uint8_t> storage(capacity + 2, 0xC7);
  AudioByteRing ring(storage.data() + 1, capacity);
  std::deque<uint8_t> expected;
  uint32_t random = static_cast<uint32_t>(capacity + 7);
  size_t produced = 0;
  for (size_t operation = 0; operation < 3000; ++operation) {
    const size_t request = nextRandom(random) % (capacity * 2 + 3);
    if (nextRandom(random) & 1) {
      std::vector<uint8_t> bytes(request);
      for (size_t i = 0; i < request; ++i) bytes[i] = static_cast<uint8_t>(produced + i);
      const size_t count = std::min(request, capacity - expected.size());
      assert(ring.write(bytes.data(), request) == count);
      expected.insert(expected.end(), bytes.begin(), bytes.begin() + count);
      produced += count;
    } else {
      std::vector<uint8_t> bytes(request + 2, 0xA9);
      const size_t count = std::min(request, expected.size());
      assert(ring.read(bytes.data() + 1, request) == count);
      for (size_t i = 0; i < count; ++i) {
        assert(bytes[i + 1] == expected.front()); expected.pop_front();
      }
      assert(bytes.front() == 0xA9);
      for (size_t i = count + 1; i < bytes.size(); ++i) assert(bytes[i] == 0xA9);
    }
    assert(ring.available() == expected.size());
    assert(ring.freeSpace() == capacity - expected.size());
    assert(storage.front() == 0xC7 && storage.back() == 0xC7);
  }
  std::vector<uint8_t> tail(capacity);
  assert(ring.read(tail.data(), tail.size()) == expected.size());
  for (size_t i = 0; !expected.empty(); ++i) {
    assert(tail[i] == expected.front()); expected.pop_front();
  }
  assert(!ring.available());
}

static void counterRollover() {
  // Advance the real public API through UINT32_MAX rather than injecting
  // internal state. A non-power-of-two ring catches counter % capacity bugs.
  constexpr size_t capacity = 65521;
  std::vector<uint8_t> storage(capacity, 0), input(capacity, 0), output(capacity, 0);
  AudioByteRing ring(storage.data(), capacity);
  const uint64_t target = static_cast<uint64_t>(UINT32_MAX) - 10;
  uint64_t moved = 0;
  while (moved < target) {
    const size_t count = static_cast<size_t>(std::min<uint64_t>(capacity, target - moved));
    assert(ring.write(input.data(), count) == count);
    assert(ring.read(output.data(), count) == count);
    moved += count;
  }
  for (size_t i = 0; i < 80; ++i) input[i] = payload(i);
  assert(ring.write(input.data(), 32) == 32);       // Producer counter rolls over.
  assert(ring.read(output.data(), 3) == 3);
  assert(ring.write(input.data() + 32, 17) == 17);  // New producer position after wrap.
  assert(ring.available() == 46 && ring.freeSpace() == capacity - 46);
  assert(ring.read(output.data() + 3, 21) == 21);   // Consumer counter rolls over.
  assert(ring.read(output.data() + 24, 25) == 25);  // New consumer position after wrap.
  for (size_t i = 0; i < 49; ++i) assert(output[i] == payload(i));
  assert(ring.available() == 0 && ring.freeSpace() == capacity);
}

static size_t concurrentTransfer(size_t capacity, size_t total) {
  std::vector<uint8_t> storage(capacity + 2, 0);
  storage.front() = storage.back() = 0xC7;
  AudioByteRing ring(storage.data() + 1, capacity);
  std::atomic<size_t> writes{0}, reads{0};
  std::atomic<bool> producerDone{false};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
  std::thread producer([&]() {
    std::array<uint8_t, 113> bytes{};
    size_t offset = 0, fragment = 0;
    while (offset < total) {
      const size_t requested = std::min(total - offset, (fragment * 31) % bytes.size() + 1);
      for (size_t i = 0; i < requested; ++i) bytes[i] = payload(offset + i);
      const size_t count = ring.write(bytes.data(), requested);
      assert(count <= requested && ring.freeSpace() <= capacity);
      offset += count; ++fragment;
      if (count) writes.fetch_add(1, std::memory_order_relaxed);
      else std::this_thread::yield();
      assert(std::chrono::steady_clock::now() < deadline);
    }
    producerDone.store(true, std::memory_order_release);
  });
  std::thread consumer([&]() {
    std::array<uint8_t, 97> bytes{};
    size_t offset = 0, fragment = 0;
    while (offset < total) {
      const size_t requested = std::min(total - offset, (fragment * 47) % bytes.size() + 1);
      bytes.fill(0);
      const size_t count = ring.read(bytes.data(), requested);
      assert(count <= requested && ring.available() <= capacity);
      for (size_t i = 0; i < count; ++i) assert(bytes[i] == payload(offset + i));
      for (size_t i = count; i < bytes.size(); ++i) assert(bytes[i] == 0);
      offset += count; ++fragment;
      if (count) reads.fetch_add(1, std::memory_order_relaxed);
      else std::this_thread::yield();
      assert(std::chrono::steady_clock::now() < deadline);
    }
  });
  producer.join(); consumer.join();
  assert(producerDone.load(std::memory_order_acquire));
  assert(ring.available() == 0 && ring.freeSpace() == capacity);
  assert(storage.front() == 0xC7 && storage.back() == 0xC7);
  return writes.load() + reads.load();
}

int main() {
  invalidAndEmpty(); fullPartialAndWrap();
  for (size_t capacity = 1; capacity <= 35; ++capacity) sequentialReference(capacity);
  for (size_t capacity : {63u, 127u, 256u, 16384u}) sequentialReference(capacity);
  counterRollover();
  size_t fragments = 0;
  for (size_t capacity : {1u, 3u, 17u, 257u, 16384u})
    fragments += concurrentTransfer(capacity, capacity == 1 ? 120000 : 1048576);
  assert(fragments > 50000);
  std::cout << "Audio byte ring: empty/full/partial/wrap/invalid/39 capacities/117000 FIFO operations/actual UINT32 rollover/"
            << fragments << " concurrent fragments/byte integrity/untouched tails passed; sizeof="
            << sizeof(AudioByteRing) << '\n';
}
