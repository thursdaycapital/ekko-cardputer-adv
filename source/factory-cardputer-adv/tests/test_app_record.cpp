// Drive the actual SD recording app, real WAV storage and asynchronous fake I2S.
#include <apps/app_record/app_record.h>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <sys/resource.h>

static FakeHal test_hal;
FakeHal& GetHAL() { return test_hal; }

void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
    if (bytes == 3 * 4096 * sizeof(int16_t)) {
        record_test::pcm_allocation_bytes = bytes;
        if (record_test::fail_record_allocation) return nullptr;
    }
    try { return ::operator new[](bytes); } catch (...) { return nullptr; }
}

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
class TemporaryCard {
public:
    std::filesystem::path root;
    TemporaryCard() {
        const char* temporary = std::getenv("TMPDIR");
        auto pattern = (std::filesystem::path(temporary ? temporary : "/tmp") / "record-app-card-XXXXXX").string();
        char* created = ::mkdtemp(pattern.data());
        if (!created) throw std::runtime_error("mkdtemp failed");
        root = created;
    }
    ~TemporaryCard() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
};
struct TestApp {
    TemporaryCard card;
    AppRecord app;
    TestApp() {
        test_hal = {};
        record_test::events.clear();
        record_test::keyboard_sfx = true;
        record_test::fail_record_allocation = false;
        record_test::pcm_allocation_bytes = 0;
        test_hal.sd_mount_point = card.root.string();
    }
    ~TestApp() { app.onClose(); }
    std::string directory() const { return (card.root / "RECORD").string(); }
};
void tick(AppRecord& app, uint32_t elapsed = 1) {
    test_hal.now += elapsed;
    app.onRunning();
    check(test_hal.delay_calls == 0, "main loop blocked on delay");
}
void press(AppRecord& app, int key = KEY_ENTER) { test_hal.keyboard.press(key); tick(app); }
bool screen(const char* text) { return test_hal.canvas.text.find(text) != std::string::npos; }
void recordBlocks(AppRecord& app, size_t blocks) {
    const int goal = test_hal.mic.next_sample + static_cast<int>(blocks * 4096);
    for (size_t iteration = 0; test_hal.mic.next_sample < goal; ++iteration) {
        check(iteration < blocks * 3 + 10, "capture made no progress");
        tick(app, 10);
        test_hal.mic.completeOne();
    }
}
void drainSave(AppRecord& app) {
    test_hal.mic.completeAll();
    tick(app);
    check(test_hal.mic.pending.empty(), "save left pending microphone buffers");
}
void beginRecord(AppRecord& app) {
    press(app, KEY_N);
    check(screen("正在录音"), "N did not start recording");
}
size_t recordAndSave(TestApp& item, size_t blocks = 6) {
    beginRecord(item.app);
    recordBlocks(item.app, blocks);
    press(item.app);
    check(screen("正在保存"), "OK did not request stop and save");
    drainSave(item.app);
    check(screen("SD卡录音"), "successful save did not return to library");
    return static_cast<size_t>(test_hal.mic.next_sample);
}
std::vector<int16_t> readPcm(const std::string& path) {
    factory_record::WavReader reader;
    check(reader.open(path), "saved file is not a valid WAV");
    check(reader.sampleRate() == 16000, "WAV sample rate changed");
    std::vector<int16_t> samples(reader.remainingSamples());
    check(reader.read(samples.data(), samples.size()) == samples.size(), "saved PCM truncated");
    return samples;
}
std::vector<factory_record::Entry> entries(TestApp& item) {
    std::string error;
    auto result = factory_record::listRecordings(item.directory(), &error);
    check(error.empty(), "library enumeration failed");
    return result;
}
void completePlayback(AppRecord& app) {
    tick(app); tick(app);
    for (size_t iteration = 0; !screen("SD卡录音"); ++iteration) {
        check(iteration < 1000, "playback did not finish");
        tick(app, 10);
        test_hal.speaker.completeOne();
    }
}
void seed(TestApp& item, size_t samples, int16_t value) {
    factory_record::WavWriter writer;
    check(writer.open(item.directory()), "fixture could not create WAV");
    std::vector<int16_t> data(samples, value);
    check(writer.append(data.data(), data.size()) && writer.finish(), "fixture could not save WAV");
}

void testLibraryAndFullSession() {
    TestApp item;
    item.app.onOpen();
    check(screen("暂无录音"), "empty card did not open in library");
    check(!test_hal.mic.isRunning(), "library unexpectedly starts microphone");
    check(!record_test::keyboard_sfx, "app must suppress key sounds during recording workflow");
    const size_t total = recordAndSave(item, 30);
    check(total > 16000, "fixture must exceed old one-second ring");
    check(record_test::pcm_allocation_bytes == 24576, "PCM storage is not bounded at 24 KiB");
    const auto files = entries(item);
    check(files.size() == 1 && files[0].dataBytes == total * 2, "full session was not saved");
    const auto samples = readPcm(files[0].path);
    check(samples.size() == total, "WAV duration was capped or padded");
    for (size_t i = 0; i < total; ++i)
        check(samples[i] == int16_t(i % 30000), "recording blocks are out of order or overwritten");
    press(item.app);
    check(screen("正在回放"), "OK did not play selected WAV");
    completePlayback(item.app);
    check(test_hal.speaker.heard == samples, "streamed playback differs from entire saved session");
    check(test_hal.speaker.maximum_pending == 2, "playback never exercised two outstanding readers");
    for (const auto& request : test_hal.speaker.submitted)
        check(request.channel == 0 && request.rate == 16000, "playback changed virtual channel or rate");
}
void testQueuedButNotStartedAndFullQueue() {
    TestApp item;
    item.app.onOpen();
    beginRecord(item.app);
    test_hal.mic.task_started = false;
    tick(item.app); tick(item.app);
    check(test_hal.mic.recordingQueueSize() == 2 && test_hal.mic.isRecording() == 0,
          "fixture does not represent published-before-task-start requests");
    const auto calls = test_hal.mic.record_calls;
    tick(item.app); tick(item.app);
    check(test_hal.mic.record_calls == calls, "queued-not-started buffers were treated as completed");
    press(item.app);
    drainSave(item.app);
    const auto files = entries(item);
    check(files.size() == 1 && readPcm(files[0].path).size() == 8192, "queued-not-started data was discarded");
}
void testCanRecordGateAndTimeout() {
    TestApp item;
    item.app.onOpen();
    beginRecord(item.app);
    test_hal.mic.allow_record = false;
    tick(item.app, 10);
    check(test_hal.mic.record_calls == 0, "record called despite unavailable published slot");
    tick(item.app, 3001);
    check(screen("停止响应"), "stalled microphone did not show actionable error");
    check(!test_hal.mic.isRunning(), "recording stall left I2S running");
}
void testStopDrainsAndHomePersists() {
    TestApp item;
    const auto config = test_hal.mic.config();
    const auto volume = test_hal.speaker.getVolume();
    item.app.onOpen();
    beginRecord(item.app);
    recordBlocks(item.app, 4);
    tick(item.app); tick(item.app);
    check(test_hal.mic.pending.size() == 2, "G0 fixture needs both pending captures");
    test_hal.homeButton.clicked = true;
    tick(item.app);
    check(!item.app.closed && screen("正在保存"), "G0 abandoned queued audio instead of draining");
    const auto calls = test_hal.mic.record_calls;
    tick(item.app);
    check(test_hal.mic.record_calls == calls, "save still submitted microphone buffers");
    drainSave(item.app);
    check(item.app.closed && record_test::keyboard_sfx, "save did not close and restore keyboard sounds");
    check(test_hal.speaker.getVolume() == volume, "close changed volume");
    check(test_hal.mic.config().magnification == config.magnification &&
          test_hal.mic.config().noise_filter_level == config.noise_filter_level, "close changed microphone config");
    check(entries(item).size() == 1, "G0 did not retain recording on SD");
    item.app.closed = false;
    item.app.onOpen();
    check(screen("REC00001.WAV"), "reopen did not load previously saved file");
    press(item.app);
    completePlayback(item.app);
    check(test_hal.speaker.heard.size() == size_t(test_hal.mic.next_sample), "reopen playback lost saved audio");
}
void testPersistentSelectionAndNoOverwrite() {
    TestApp item;
    seed(item, 37, 11); seed(item, 73, 22);
    item.app.onOpen();
    press(item.app, KEY_SEMICOLON);
    press(item.app);
    completePlayback(item.app);
    check(test_hal.speaker.heard == std::vector<int16_t>(37, 11), "up did not select older recording");
    test_hal.speaker.heard.clear();
    press(item.app, KEY_DOT);
    press(item.app);
    completePlayback(item.app);
    check(test_hal.speaker.heard == std::vector<int16_t>(73, 22), "down did not select newer recording");
    recordAndSave(item, 3);
    const auto files = entries(item);
    check(files.size() == 3 && files[0].name == "REC00003.WAV", "new recording overwrote existing name");
    check(readPcm(files[2].path) == std::vector<int16_t>(37, 11), "old recording content changed");
}
void testPlaybackGateStopAndClose() {
    TestApp item;
    seed(item, 18000, 19);
    item.app.onOpen();
    press(item.app);
    test_hal.speaker.allow_play = false;
    tick(item.app);
    check(test_hal.speaker.submitted.empty(), "playRaw called despite canPlay=false");
    test_hal.speaker.allow_play = true;
    tick(item.app); tick(item.app);
    check(test_hal.speaker.pending.size() == 2, "playback fixture needs full queue");
    const auto calls = test_hal.speaker.submitted.size();
    tick(item.app);
    check(test_hal.speaker.submitted.size() == calls, "full speaker queue caused extra blocking submission");
    press(item.app);
    check(screen("SD卡录音") && test_hal.speaker.pending.empty(), "OK did not cancel active playback");
    press(item.app); tick(item.app); tick(item.app);
    test_hal.homeButton.clicked = true;
    tick(item.app);
    check(item.app.closed && test_hal.speaker.pending.empty(), "G0 left playback reader owning released memory");
}
void testSdMissingDirectoryErrorAndRetry() {
    TestApp item;
    test_hal.sd_mounted = false;
    item.app.onOpen();
    check(screen("未检测") && record_test::pcm_allocation_bytes == 0, "missing card masquerades as recording");
    test_hal.sd_mounted = true;
    press(item.app);
    check(screen("暂无录音"), "inserting card and OK did not recover");
    item.app.onClose();
    { std::ofstream invalid(item.directory()); invalid << "retain this user file"; }
    item.app.onOpen();
    check(screen("目录读取失败"), "non-directory RECORD path was silently replaced");
    std::ifstream original(item.directory());
    std::string data; std::getline(original, data);
    check(data == "retain this user file", "app destroyed conflicting user file");
}
void testAllocationAndMicFailureRetry() {
    TestApp item;
    record_test::fail_record_allocation = true;
    item.app.onOpen();
    check(screen("内存不足"), "PCM allocation failure lacks error");
    record_test::fail_record_allocation = false;
    press(item.app);
    test_hal.mic.fail_begin = true;
    press(item.app, KEY_N);
    check(screen("麦克风启动失败") && test_hal.mic.submitted.empty(), "failed microphone queued audio");
    test_hal.mic.fail_begin = false;
    press(item.app);
    beginRecord(item.app);
    recordBlocks(item.app, 2);
}
void testCaptureFailureAndRetry() {
    TestApp item;
    item.app.onOpen();
    beginRecord(item.app);
    test_hal.mic.fail_record = true;
    tick(item.app);
    check(screen("录音采集失败") && test_hal.mic.pending.empty(), "record rejection did not cancel capture");
    test_hal.mic.fail_record = false;
    press(item.app);
    recordAndSave(item, 2);
}
void testSpeakerFailures() {
    TestApp item;
    seed(item, 16000, 42);
    item.app.onOpen();
    test_hal.speaker.fail_begin = true;
    press(item.app);
    check(screen("扬声器启动失败") && test_hal.speaker.submitted.empty(), "failed speaker submitted PCM");
    test_hal.speaker.fail_begin = false;
    press(item.app);
    press(item.app);
    test_hal.speaker.fail_play = true;
    tick(item.app);
    check(screen("录音播放失败") && test_hal.speaker.pending.empty(), "play rejection masquerades as playback");
    test_hal.speaker.fail_play = false;
}
void testDrainAndPlaybackTimeout() {
    TestApp item;
    item.app.onOpen();
    beginRecord(item.app);
    tick(item.app); tick(item.app);
    press(item.app);
    tick(item.app, 3001);
    check(screen("停止响应") && test_hal.mic.pending.empty(), "drain stall did not cancel pending I2S");
    press(item.app);
    seed(item, 20000, 23);
    item.app.onClose(); item.app.onOpen();
    press(item.app); tick(item.app); tick(item.app);
    tick(item.app, 3001);
    check(screen("播放停止响应") && test_hal.speaker.pending.empty(), "playback stall did not release readers");
}
void testTruncatedPlaybackRead() {
    TestApp item;
    seed(item, 16000, 77);
    item.app.onOpen();
    press(item.app);
    const auto files = entries(item);
    check(::truncate(files[0].path.c_str(), 44) == 0, "fixture failed to truncate externally removed PCM");
    tick(item.app);
    check(screen("读取中断") && test_hal.speaker.pending.empty(), "truncated WAV was played as valid data");
}
void testWriteFailurePreservesPartial() {
    TestApp item;
    item.app.onOpen();
    beginRecord(item.app);
    tick(item.app);
    test_hal.mic.completeOne();
    struct rlimit previous;
    check(::getrlimit(RLIMIT_FSIZE, &previous) == 0, "cannot inspect file size resource limit");
    const auto old_signal = std::signal(SIGXFSZ, SIG_IGN);
    auto limited = previous;
    limited.rlim_cur = 44 + 2001;
    check(::setrlimit(RLIMIT_FSIZE, &limited) == 0, "cannot set file size fault fixture");
    try { tick(item.app); }
    catch (...) { ::setrlimit(RLIMIT_FSIZE, &previous); std::signal(SIGXFSZ, old_signal); throw; }
    check(::setrlimit(RLIMIT_FSIZE, &previous) == 0, "cannot restore file size limit");
    std::signal(SIGXFSZ, old_signal);
    check(screen("写入失败") && test_hal.mic.pending.empty(), "disk-full failure left capture running");
    const auto files = entries(item);
    check(files.size() == 1 && files[0].dataBytes == 2000, "complete samples from short write were not retained");
    const auto samples = readPcm(files[0].path);
    for (size_t i = 0; i < samples.size(); ++i)
        check(samples[i] == int16_t(i % 30000), "partial recording contains corrupt PCM");
}
void testFreshAppInstanceReloadsWav() {
    TestApp item;
    item.app.onOpen();
    // Empty library also supports OK, without requiring N.
    press(item.app);
    check(screen("正在录音"), "empty library OK did not start recording");
    recordBlocks(item.app, 7);
    press(item.app); drainSave(item.app);
    const auto files = entries(item);
    const auto saved = readPcm(files[0].path);
    item.app.onClose();
    AppRecord restarted;
    restarted.onOpen();
    check(screen("REC00001.WAV"), "fresh app instance did not discover saved WAV");
    press(restarted); completePlayback(restarted);
    check(test_hal.speaker.heard == saved, "new app instance depended on old RAM recording");
    restarted.onClose();
}
void testWaveformThrottleAndReopenCleanup() {
    TestApp item;
    item.app.onOpen();
    beginRecord(item.app);
    const auto pushes = test_hal.pushes;
    for (size_t i = 0; i < 20; ++i) { tick(item.app, 1); test_hal.mic.completeOne(); }
    check(test_hal.pushes == pushes, "waveform redraw ignores 100ms throttle");
    // Forced lifecycle shutdown must cancel every owner before freeing the pool.
    tick(item.app); tick(item.app);
    item.app.onClose();
    check(test_hal.mic.pending.empty() && test_hal.speaker.pending.empty(), "close left asynchronous pointers active");
    check(record_test::keyboard_sfx, "close did not restore keyboard sounds");
    item.app.onOpen();
    check(screen("SD卡录音"), "reopen did not reset state to persistent library");
    recordAndSave(item, 2);
}
}
int main() {
    const struct { const char* name; void (*run)(); } tests[] = {
        {"full-session WAV save and chronological same-channel streaming", testLibraryAndFullSession},
        {"published microphone descriptors before task starts", testQueuedButNotStartedAndFullQueue},
        {"canRecord gate and stalled capture", testCanRecordGateAndTimeout},
        {"G0 drains, saves, closes, restores and reopens", testStopDrainsAndHomePersists},
        {"persistent selection and non-overwriting names", testPersistentSelectionAndNoOverwrite},
        {"canPlay gate, full queue, OK stop and G0 close", testPlaybackGateStopAndClose},
        {"missing SD, conflicting user file and retry", testSdMissingDirectoryErrorAndRetry},
        {"24 KiB allocation and microphone begin failure retry", testAllocationAndMicFailureRetry},
        {"microphone queue failure and retry", testCaptureFailureAndRetry},
        {"speaker begin and play rejection", testSpeakerFailures},
        {"drain and playback stall cancellation", testDrainAndPlaybackTimeout},
        {"external WAV truncation while reader is open", testTruncatedPlaybackRead},
        {"short write retains complete PCM samples", testWriteFailurePreservesPartial},
        {"fresh app instance reloads SD WAV independent of old RAM", testFreshAppInstanceReloadsWav},
        {"waveform throttle and forced close buffer ownership", testWaveformThrottleAndReopenCleanup},
    };
    for (const auto& test : tests) {
        try { test.run(); std::cout << "PASS: " << test.name << '\n'; }
        catch (const std::exception& error) { std::cerr << "FAIL: " << test.name << ": " << error.what() << '\n'; return 1; }
    }
    std::cout << "15 production AppRecord SD host tests passed under ASan/UBSan. Physical SD/I2S timing and sound are not simulated.\n";
}
