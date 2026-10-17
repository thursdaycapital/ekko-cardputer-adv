#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

// The network producer owns write()/freeSpace(); the decoder consumer owns
// read()/available(). Neither side may have a second concurrent caller.
// Storage belongs to the caller and must outlive both sides. Its memory must
// not overlap a read/write source or destination. There is no allocation here.
// Observations from any third thread are bounded diagnostic snapshots only.
class AudioByteRing {
 public:
  AudioByteRing(uint8_t* storage, size_t capacity)
      : storage_(storage), capacity_(storage && capacity &&
            capacity <= std::numeric_limits<uint32_t>::max() / 2
                ? static_cast<uint32_t>(capacity) : 0) {}

  AudioByteRing(const AudioByteRing&) = delete;
  AudioByteRing& operator=(const AudioByteRing&) = delete;

  bool valid() const { return capacity_ != 0; }
  size_t capacity() const { return capacity_; }

  size_t available() const {
    const uint32_t read = readCount_.load(std::memory_order_relaxed);
    const uint32_t written = writeCount_.load(std::memory_order_acquire);
    return boundedDistance(written, read);
  }

  size_t freeSpace() const {
    const uint32_t written = writeCount_.load(std::memory_order_relaxed);
    const uint32_t read = readCount_.load(std::memory_order_acquire);
    return capacity_ - boundedDistance(written, read);
  }

  size_t write(const uint8_t* source, size_t length) {
    if (!valid() || !source || !length) return 0;
    const uint32_t written = writeCount_.load(std::memory_order_relaxed);
    const uint32_t read = readCount_.load(std::memory_order_acquire);
    const size_t space = capacity_ - boundedDistance(written, read);
    const size_t count = length < space ? length : space;
    if (!count) return 0;
    const size_t tail = capacity_ - writePosition_;
    const size_t first = count < tail ? count : tail;
    std::memcpy(storage_ + writePosition_, source, first);
    if (count > first) std::memcpy(storage_, source + first, count - first);
    writePosition_ = advance(writePosition_, static_cast<uint32_t>(count));
    // Publish bytes only after both copies. Acquire on read's counter stops
    // this producer from overwriting storage that the consumer is still using.
    writeCount_.store(written + static_cast<uint32_t>(count), std::memory_order_release);
    return count;
  }

  size_t read(uint8_t* destination, size_t length) {
    if (!valid() || !destination || !length) return 0;
    const uint32_t read = readCount_.load(std::memory_order_relaxed);
    const uint32_t written = writeCount_.load(std::memory_order_acquire);
    const size_t ready = boundedDistance(written, read);
    const size_t count = length < ready ? length : ready;
    if (!count) return 0;
    const size_t tail = capacity_ - readPosition_;
    const size_t first = count < tail ? count : tail;
    std::memcpy(destination, storage_ + readPosition_, first);
    if (count > first) std::memcpy(destination + first, storage_, count - first);
    readPosition_ = advance(readPosition_, static_cast<uint32_t>(count));
    readCount_.store(read + static_cast<uint32_t>(count), std::memory_order_release);
    return count;
  }

 private:
  static_assert((std::is_same<uint32_t, unsigned int>::value && ATOMIC_INT_LOCK_FREE == 2) ||
                    (std::is_same<uint32_t, unsigned long>::value && ATOMIC_LONG_LOCK_FREE == 2),
                "AudioByteRing requires lock-free uint32_t atomics");

  uint32_t boundedDistance(uint32_t written, uint32_t read) const {
    const uint32_t distance = written - read;  // Unsigned wrap is intentional.
    return distance < capacity_ ? distance : capacity_;
  }

  uint32_t advance(uint32_t position, uint32_t length) const {
    // Independent positions also work for non-power-of-two capacities when
    // the monotonically increasing uint32_t counters wrap at UINT32_MAX.
    position += length;  // Each operand <= UINT32_MAX / 2, so this cannot wrap.
    return position < capacity_ ? position : position - capacity_;
  }

  uint8_t* const storage_;
  const uint32_t capacity_;
  std::atomic<uint32_t> writeCount_{0};
  std::atomic<uint32_t> readCount_{0};
  uint32_t writePosition_ = 0;  // Producer only.
  uint32_t readPosition_ = 0;   // Consumer only.
};
