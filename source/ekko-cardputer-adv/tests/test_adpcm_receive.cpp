#include "AudioByteRing.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

// Hardware/OS boundary substitutes; receive/decode/ring algorithms come from
// the generated production header below, never from a test reimplementation.
static std::atomic<bool> failReceiveAllocation{false};
static std::atomic<unsigned> arrayAllocationAttempts{0};
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
  ++arrayAllocationAttempts;
  if (failReceiveAllocation.load()) return nullptr;
  try { return ::operator new[](bytes); } catch (...) { return nullptr; }
}

using std::min;
using std::max;
#define F(text) text
class String : public std::string {
 public:
  using std::string::operator=;
  String() = default;
  String(const char* text) : std::string(text) {}
  String(const std::string& text) : std::string(text) {}
  explicit String(int value) : std::string(std::to_string(value)) {}
  explicit String(unsigned int value) : std::string(std::to_string(value)) {}
  explicit String(unsigned long value) : std::string(std::to_string(value)) {}
};
struct SerialBoundary { template<class... Args> void printf(const char*, Args...) {} } Serial;
struct EspBoundary {
  uint32_t heap = 100000;
  uint32_t maxAllocation = 100000;
  uint32_t getFreeHeap() const { return heap; }
  uint32_t getMinFreeHeap() const { return heap; }
  uint32_t getMaxAllocHeap() const { return maxAllocation; }
} ESP;

using TickType_t = uint32_t;
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1;
static const auto epoch = std::chrono::steady_clock::now();
static std::atomic<uint32_t> clockOffset{0};
static uint32_t millis() {
  return 1 + clockOffset.load() + static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - epoch).count());
}
static uint32_t micros() {
  return 1000 + clockOffset.load() * 1000 + static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - epoch).count());
}
static void yield() { std::this_thread::yield(); }
static void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static void vTaskDelay(uint32_t) { std::this_thread::sleep_for(std::chrono::microseconds(100)); }
static uint32_t pdMS_TO_TICKS(uint32_t ms) { return ms; }

class WiFiClient {
 public:
  explicit WiFiClient(std::vector<uint8_t> input = {}) : bytes(std::move(input)) {}
  std::vector<uint8_t> bytes;
  std::atomic<size_t> offset{0};
  size_t fragment = 1024;
  bool closeAtEnd = true, forceTimeout = false;
  std::atomic<bool> holdLastByte{false};
  std::atomic<unsigned> calls{0};
  unsigned readCalls = 0;
  size_t largestRead = 0;
  int available() {
    ++calls;
    const size_t position = offset.load();
    size_t end = bytes.size();
    if (holdLastByte.load() && end) --end;
    return static_cast<int>(position < end ? min(fragment, end - position) : 0);
  }
  int read(uint8_t* output, size_t requested) {
    // Production only calls bounded reads after observing available()>0. No
    // artificial blocking read or worker is used in this cooperative harness.
    assert(requested > 0 && requested <= static_cast<size_t>(available()));
    ++readCalls; largestRead = max(largestRead, requested);
    const size_t position = offset.load();
    const size_t count = min(requested, bytes.size() - position);
    if (!count) return 0;
    std::memcpy(output, bytes.data() + position, count);
    offset.store(position + count);
    return static_cast<int>(count);
  }
  bool connected() {
    if (forceTimeout) { clockOffset.fetch_add(30001); forceTimeout = false; }
    return !closeAtEnd || offset.load() < bytes.size();
  }
};

static bool audioBusy = false, i2sReady = true, es8311Ready = true;
static bool audioUrlCancelled = false, mcuVoiceAfterAudioInterrupt = false;
static bool mcuAudioStopOnlyAfterInterrupt = false, mcuSessionClearAfterAudioInterrupt = false;
static String lastAudioDetail, mcuInteractionId("host-test"), interactionStatus;
static uint32_t lastAudioAtMs = 0;
static std::vector<int16_t> playedStereo;
static std::atomic<uint32_t> writtenFrames{0};
static std::function<bool()> interruptHook;
static bool failWrite = false, failDrain = false;
static unsigned drained = 0, cleared = 0;
static bool shouldInterruptAudioForVoice() { return interruptHook && interruptHook(); }
static void mcuSocketLoop() { yield(); }
static void refreshMcuSubtitlePage() {}
static void markMcuInteraction(const String&, const char* status, const String&) { interactionStatus = status; }
static int16_t shapeOutputSample(int16_t sample) { return sample; }
static void updateMcuAudioPlaybackProgress(uint32_t, uint32_t) {}
static void setMcuAudioTimelineFromFrames(uint32_t, uint32_t) {}
static void setPowerAmp(bool) {}
static bool setI2sSampleRate(uint32_t) { return true; }
static void cardputerAudioClear() { ++cleared; }
struct NativeAudioPlaybackScope { ~NativeAudioPlaybackScope() { cardputerAudioClear(); } };
static esp_err_t cardputerAudioWrite(const void* data, size_t bytes, size_t* written, TickType_t) {
  if (failWrite) { *written = 0; return ESP_FAIL; }
  assert(bytes && bytes % 4 == 0);
  const auto* samples = static_cast<const int16_t*>(data);
  playedStereo.insert(playedStereo.end(), samples, samples + bytes / sizeof(int16_t));
  *written = bytes; writtenFrames.fetch_add(static_cast<uint32_t>(bytes / 4));
  yield(); return ESP_OK;
}
static bool drainI2sPlayback(uint32_t bytes, unsigned channels, uint32_t rate) {
  assert(bytes == playedStereo.size() * sizeof(int16_t) && channels == 2 && rate);
  ++drained; return !failDrain;
}

#include "firmware_receive_under_test.h"

// The transport now allocates compressed storage before starting TLS. This
// overload supplies an external owner for the existing playback test cases;
// the production five-argument function must neither allocate nor free it.
static bool playAdpcmStream(WiFiClient* client, int length, uint32_t sampleRate) {
  std::vector<uint8_t> storage(kMcuAdpcmReceiveBytes);
  return playAdpcmStream(client, length, sampleRate, storage.data(), storage.size());
}

static std::vector<uint8_t> hadp(uint32_t samples, uint32_t rate = 24000) {
  std::vector<uint8_t> bytes(20 + samples / 2, 0);
  std::memcpy(bytes.data(), "HADP", 4); bytes[4] = bytes[5] = 1;
  for (unsigned i = 0; i < 4; ++i) {
    bytes[8 + i] = static_cast<uint8_t>(rate >> (8 * i));
    bytes[12 + i] = static_cast<uint8_t>(samples >> (8 * i));
  }
  // Predictor 1000, zero nibbles/index: exactly constant nonzero PCM. The
  // independent expected output catches unproduced zero-filled tail data.
  bytes[16] = 0xE8; bytes[17] = 0x03;
  return bytes;
}
static void reset() {
  audioBusy = false; ESP.heap = ESP.maxAllocation = 100000; interactionStatus.clear();
  lastAudioDetail.clear(); lastAdpcmReceive = AdpcmReceiveReport();
  playedStereo.clear(); writtenFrames.store(0); interruptHook = {};
  failWrite = failDrain = false; failReceiveAllocation.store(false);
  audioUrlCancelled = mcuVoiceAfterAudioInterrupt = mcuAudioStopOnlyAfterInterrupt = mcuSessionClearAfterAudioInterrupt = false;
  drained = cleared = 0;
}
static void expectPlayback(uint32_t samples, bool unknownLength, size_t fragment) {
  reset(); WiFiClient client(hadp(samples)); client.fragment = fragment;
  client.closeAtEnd = !unknownLength; // Unknown length deliberately keeps TCP open.
  assert(playAdpcmStream(&client, unknownLength ? -1 : static_cast<int>(client.bytes.size()), 24000));
  assert(!audioBusy && lastAdpcmReceive.complete && std::string(lastAdpcmReceive.result) == "complete");
  assert(lastAdpcmReceive.expectedBytes == samples / 2 && lastAdpcmReceive.receivedBytes == samples / 2);
  assert(client.offset.load() == client.bytes.size());
  assert(playedStereo.size() == samples * 2 && drained == 1 && cleared == 1);
  for (auto value : playedStereo) assert(value == 1000);
}
static void playbackBoundaries() {
  for (uint32_t count : {1u, 2u, 3u, 255u, 256u, 257u, 511u, 512u, 513u, 23999u, 24000u, 24001u, 98305u})
    for (bool unknown : {false, true}) expectPlayback(count, unknown, count % 19 + 1);
  reset(); WiFiClient empty(hadp(0));
  assert(!playAdpcmStream(&empty, 20, 24000));
  assert(!lastAdpcmReceive.complete && std::string(lastAdpcmReceive.result) == "empty");
  for (int length : {0, 1, 19, 20, 30}) {
    reset(); WiFiClient mismatch(hadp(31));
    assert(!playAdpcmStream(&mismatch, length, 24000));
    assert(!lastAdpcmReceive.complete && playedStereo.empty());
  }
  reset(); WiFiClient badIndex(hadp(31)); badIndex.bytes[18] = 89;
  assert(!playAdpcmStream(&badIndex, static_cast<int>(badIndex.bytes.size()), 24000));
  reset(); WiFiClient badMagic(hadp(31)); badMagic.bytes[0] = 'X';
  assert(!playAdpcmStream(&badMagic, static_cast<int>(badMagic.bytes.size()), 24000));
  for (uint32_t rate : {7999u, 96001u}) {
    reset(); WiFiClient badRate(hadp(31, rate));
    assert(!playAdpcmStream(&badRate, static_cast<int>(badRate.bytes.size()), 24000));
    assert(!lastAdpcmReceive.complete && playedStereo.empty());
  }
  reset(); WiFiClient shortHeader(hadp(31)); shortHeader.bytes.resize(13);
  assert(!playAdpcmStream(&shortHeader, -1, 24000));
  assert(!lastAdpcmReceive.complete && playedStereo.empty());
  reset(); WiFiClient trailing(hadp(1025)); const size_t expectedEnd = trailing.bytes.size();
  trailing.bytes.insert(trailing.bytes.end(), {0xEE, 0xEF, 0xF0, 0xF1}); trailing.closeAtEnd = false;
  assert(playAdpcmStream(&trailing, -1, 24000));
  assert(trailing.offset.load() == expectedEnd && playedStereo.size() == 2050);
}
static void truncatedTimeoutAndFailures() {
  for (bool unknown : {false, true}) {
    reset(); WiFiClient client(hadp(30001)); const int originalLength = static_cast<int>(client.bytes.size());
    client.bytes.resize(client.bytes.size() - 27);
    assert(!playAdpcmStream(&client, unknown ? -1 : originalLength, 24000));
    assert(!lastAdpcmReceive.complete && !audioBusy);
    assert(lastAdpcmReceive.receivedBytes == client.bytes.size() - 20);
  }
  // More than two rings of declared payload: the consumer must have started
  // before the producer reaches the missing tail, so this exercises failures
  // during playback rather than just an already-failed prebuffer.
  reset(); WiFiClient lateTruncation(hadp(100001));
  const int declaredLength = static_cast<int>(lateTruncation.bytes.size());
  lateTruncation.bytes.resize(lateTruncation.bytes.size() - 1234);
  assert(!playAdpcmStream(&lateTruncation, declaredLength, 24000));
  assert(!lastAdpcmReceive.complete && !audioBusy && !playedStereo.empty() && cleared == 1);
  reset(); WiFiClient timeout(hadp(40001)); timeout.bytes.resize(20);
  timeout.closeAtEnd = false; timeout.forceTimeout = true;
  assert(!playAdpcmStream(&timeout, -1, 24000));
  assert(!lastAdpcmReceive.complete && lastAdpcmReceive.emptyPolls);
  for (unsigned failure = 0; failure < 4; ++failure) {
    reset(); WiFiClient client(hadp(1025));
    if (failure == 0) ESP.heap = 16383; // External buffer already allocated; no task stack.
    if (failure == 2) failWrite = true;
    if (failure == 3) failDrain = true;
    const bool played = failure == 1
        ? playAdpcmStream(&client, static_cast<int>(client.bytes.size()), 24000, nullptr, 0)
        : playAdpcmStream(&client, static_cast<int>(client.bytes.size()), 24000);
    assert(!played);
    assert(!lastAdpcmReceive.complete && !audioBusy);
  }
}
static void externallyOwnedReceiveBuffers() {
  for (size_t capacity : {8192u, 16384u}) {
    for (uint32_t samples : {1u, 3u, 65539u}) {
      for (bool unknown : {false, true}) {
        reset(); std::vector<uint8_t> storage(capacity);
        WiFiClient client(hadp(samples)); client.closeAtEnd = !unknown;
        // Supplying an owned buffer must work even when new[] is unavailable
        // after TLS. This also rejects accidental reintroduction of an inner
        // allocation rather than merely checking a source-code string.
        failReceiveAllocation.store(true);
        const unsigned allocations = arrayAllocationAttempts.load();
        assert(playAdpcmStream(&client, unknown ? -1 : static_cast<int>(client.bytes.size()),
                               24000, storage.data(), storage.size()));
        assert(arrayAllocationAttempts.load() == allocations);
        assert(lastAdpcmReceive.complete && lastAdpcmReceive.bufferBytes == capacity);
        assert(playedStereo.size() == samples * 2);
        for (auto value : playedStereo) assert(value == 1000);
      }
    }
  }
  for (unsigned invalid = 0; invalid < 3; ++invalid) {
    reset(); std::vector<uint8_t> storage(8192); WiFiClient client(hadp(1025));
    uint8_t* buffer = invalid == 0 ? storage.data() : nullptr;
    size_t capacity = invalid == 1 ? storage.size() : 0;
    assert(!playAdpcmStream(&client, -1, 24000, buffer, capacity));
    assert(!lastAdpcmReceive.complete && playedStereo.empty());
  }
  reset(); std::vector<uint8_t> storage(8192); WiFiClient exactBudget(hadp(1025));
  ESP.heap = 16384;
  assert(playAdpcmStream(&exactBudget, -1, 24000, storage.data(), storage.size()));
  assert(lastAdpcmReceive.complete);
}
static void pollUntilDone(AdpcmDownload& receiver) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!receiver.done()) {
    assert(std::chrono::steady_clock::now() < deadline);
    receiver.poll();
  }
}
static void downloadPollStates() {
  reset(); uint8_t storage[7] = {};
  WiFiClient exact({1, 2, 3, 4, 5});
  {
    AdpcmDownload rx(&exact, storage, sizeof(storage), 5); assert(rx.start());
    assert(exact.calls.load() == 0); // start() must not access the client.
    pollUntilDone(rx);
    assert(rx.result() == AdpcmDownload::Complete && rx.ring.available() == 5);
    uint8_t bytes[7] = {}; assert(rx.ring.read(bytes, sizeof(bytes)) == 5);
    for (unsigned i = 0; i < 5; ++i) assert(bytes[i] == i + 1);
    rx.cancel(); assert(rx.result() == AdpcmDownload::Complete);
  }
  assert(lastAdpcmReceive.receivedBytes == 5);
  reset(); WiFiClient empty;
  {
    AdpcmDownload rx(&empty, storage, sizeof(storage), 0); assert(rx.start());
    pollUntilDone(rx); assert(rx.result() == AdpcmDownload::Complete && !rx.ring.available());
    assert(empty.readCalls == 0);
  }
  reset(); WiFiClient shortBody({1, 2});
  {
    AdpcmDownload rx(&shortBody, storage, sizeof(storage), 5); assert(rx.start());
    pollUntilDone(rx); assert(rx.result() == AdpcmDownload::Truncated);
    assert(rx.ring.available() == 2); // A failed EOF still owns its received tail.
  }
  reset(); WiFiClient timedOut; timedOut.closeAtEnd = false; timedOut.forceTimeout = true;
  {
    AdpcmDownload rx(&timedOut, storage, sizeof(storage), 5); assert(rx.start());
    pollUntilDone(rx); assert(rx.result() == AdpcmDownload::TimedOut);
  }
  reset(); WiFiClient backpressure(std::vector<uint8_t>(100, 1));
  {
    AdpcmDownload rx(&backpressure, storage, sizeof(storage), 100); assert(rx.start());
    rx.poll(); assert(rx.ring.available() == sizeof(storage));
    clockOffset.fetch_add(kMcuAudioHttpTimeoutMs + 1);
    rx.poll(); // Consumer backpressure is not a network timeout.
    assert(rx.result() == AdpcmDownload::Running && rx.ring.available() == sizeof(storage));
    rx.cancel(); assert(rx.result() == AdpcmDownload::Cancelled);
    assert(backpressure.offset.load() == sizeof(storage));
    const unsigned calls = backpressure.calls.load();
    rx.poll(); assert(backpressure.calls.load() == calls); // Cancel is final.
  }
  reset(); WiFiClient invalid;
  { AdpcmDownload rx(&invalid, nullptr, 7, 1); assert(!rx.start()); }
  { AdpcmDownload rx(&invalid, storage, 0, 1); assert(!rx.start()); }
  { AdpcmDownload rx(nullptr, storage, sizeof(storage), 1); assert(!rx.start()); }
  reset(); WiFiClient cancelled({1, 2, 3});
  {
    AdpcmDownload rx(&cancelled, storage, sizeof(storage), 3); assert(rx.start());
    rx.cancel(); assert(rx.result() == AdpcmDownload::Cancelled);
    rx.poll(); assert(cancelled.calls.load() == 0 && cancelled.readCalls == 0);
  }
  reset(); std::vector<uint8_t> largeStorage(16384); WiFiClient bulk(std::vector<uint8_t>(100000, 1));
  {
    AdpcmDownload rx(&bulk, largeStorage.data(), largeStorage.size(), 100000); assert(rx.start());
    rx.poll();
    assert(bulk.offset.load() > 0 && bulk.offset.load() <= 4096 && bulk.largestRead <= 1024);
    assert(!rx.done()); rx.cancel();
  }
}
static void cancellationAndFinalPublication() {
  reset(); WiFiClient prebuffer(hadp(40001)); prebuffer.bytes.resize(24); prebuffer.closeAtEnd = false;
  interruptHook = [&]() { return prebuffer.offset.load() == 24; };
  assert(!playAdpcmStream(&prebuffer, -1, 24000));
  assert(std::string(lastAdpcmReceive.result) == "cancelled_prebuffer" && !audioBusy);
  reset(); WiFiClient duringPlay(hadp(60001));
  interruptHook = [&]() { return writtenFrames.load() >= 512; };
  assert(!playAdpcmStream(&duringPlay, -1, 24000));
  assert(std::string(lastAdpcmReceive.result) == "cancelled_playback" && cleared == 1 && !audioBusy);
  // Hold the final payload byte while the real decoder drains the ring, then
  // release it on a later main-loop tick. No second thread or blocking network
  // read is involved: only the real poll() may fetch and publish the final byte.
  for (unsigned attempt = 0; attempt < 32; ++attempt) {
    reset(); constexpr uint32_t samples = 65539;
    WiFiClient tail(hadp(samples)); tail.holdLastByte.store(true);
    interruptHook = [&]() {
      if (writtenFrames.load() >= samples - 513) tail.holdLastByte.store(false);
      return false;
    };
    assert(playAdpcmStream(&tail, -1, 24000));
    assert(lastAdpcmReceive.complete && playedStereo.size() == samples * 2);
  }
}
int main() {
  playbackBoundaries(); truncatedTimeoutAndFailures(); externallyOwnedReceiveBuffers();
  downloadPollStates(); cancellationAndFinalPublication();
  reset();
  std::cout << "Production ADPCM receive/playback: 26 known/unknown-length short/long cases; empty/malformed/"
      "truncated/timeout/heap/missing-buffer/I2S failures; external 8/16KiB buffers/no inner new[]; "
      "bounded bulk poll/full backpressure/final cancellation; prebuffer/playback cancellation; "
      "32 deferred final-byte publication cases passed; no worker tasks\n";
}
