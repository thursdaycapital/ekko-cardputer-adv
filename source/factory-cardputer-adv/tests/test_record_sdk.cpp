#include "record_sdk_host.h"
#include <iostream>
#include <string>

namespace sdk_test { Runtime runtime; }

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void reset() { sdk_test::runtime = {}; }

void checkEmpty(const m5::Mic_Class& microphone) {
    for (const auto& slot : microphone._rec_info) {
        check(slot.data == nullptr && slot.length == 0 && slot.index == 0 &&
              !slot.is_stereo && !slot.is_16bit, "Mic.end retained an old recording descriptor");
    }
    check(!microphone._rec_flip && !microphone._is_recording, "Mic.end retained queue flags");
}

void testIdleEndClearsDescriptors() {
    reset();
    m5::Mic_Class microphone;
    int16_t recording[8]{};
    microphone._rec_info[0] = {recording, 8, 3, true, true};
    microphone._rec_info[1] = {recording + 4, 4, 2, true, true};
    microphone._rec_flip = true;
    microphone._is_recording = true;
    microphone.end();
    checkEmpty(microphone);
}

void testJoinThenReleaseAndReopen() {
    reset();
    m5::Mic_Class microphone;
    check(microphone.begin(), "microphone success fixture failed to begin");
    auto first = std::make_unique<int16_t[]>(200);
    auto second = std::make_unique<int16_t[]>(200);
    microphone._rec_info[0] = {first.get(), 200, 0, false, true};
    microphone._rec_info[1] = {second.get(), 200, 0, false, true};
    microphone._rec_flip = true;
    microphone._is_recording = true;
    microphone.end();
    check(!microphone._task_running && microphone._task_handle == nullptr,
          "Mic.end returned before the capture task stopped");
    checkEmpty(microphone);
    first.reset();
    second.reset();

    // Re-enter the actual SDK capture task after the old arrays were freed.
    // A stale descriptor would be consumed here and ASan would detect it.
    sdk_test::runtime.read_calls = 0;
    sdk_test::runtime.on_wait = [&] { microphone._task_running = false; };
    check(microphone.begin(), "microphone failed to reopen after cancellation");
    m5::Mic_Class::mic_task(&microphone);
    check(sdk_test::runtime.read_calls == 2, "reopened microphone consumed an old queued recording");
    check(sdk_test::runtime.notify_waits == 1, "reopened capture did not find an empty queue");
    microphone.end();
}

void testCaptureLoopCancellation() {
    reset();
    m5::Mic_Class microphone;
    int16_t recording[200]{};
    microphone._task_running = true;
    microphone._rec_info[1] = {recording, 200, 0, false, true};
    sdk_test::runtime.on_read = [&] {
        if (sdk_test::runtime.read_calls == 3) {
            microphone._task_running = false;
            sdk_test::runtime.read_result_bytes = 0;
        }
    };
    m5::Mic_Class::mic_task(&microphone);
    check(sdk_test::runtime.read_calls <= 3, "cancelled microphone continued its inner capture loop");
    check(microphone._task_handle == nullptr && !microphone._is_recording,
          "cancelled microphone task did not exit");
    check(sdk_test::runtime.stopped == 1, "cancelled microphone did not stop I2S");
    microphone.end();
    checkEmpty(microphone);
}

void testRecordingQueueBeforeTaskAndAfterCompletion() {
    reset();
    m5::Mic_Class microphone;
    int16_t first[8], second[8];
    std::fill_n(first, 8, int16_t(1234));
    std::fill_n(second, 8, int16_t(1234));
    check(microphone.recordingQueueSize() == 0 && microphone.canRecord(), "idle microphone queue is not available");
    check(microphone._rec_raw(first, 8, true, 16000, false), "first actual recording enqueue failed");
    check(microphone.isRecording() == 0, "legacy activity-gated isRecording behavior changed");
    check(microphone.recordingQueueSize() == 1, "new recording API lost a queued-before-task descriptor");
    check(!microphone.canRecord(), "queue count below two incorrectly promised a nonblocking record");
    // The actual task advances this marker before it begins filling slot zero.
    __atomic_store_n(&microphone._rec_flip, true, __ATOMIC_RELEASE);
    check(microphone.canRecord(), "next free descriptor was not available after capture advanced");
    check(microphone._rec_raw(second, 8, true, 16000, false), "second actual recording enqueue failed");
    check(microphone.recordingQueueSize() == 2 && !microphone.canRecord(), "two queued captures lost buffer ownership");

    sdk_test::runtime.on_wait = [&] {
        check(microphone.recordingQueueSize() == 0, "completed capture kept ownership of destination buffers");
        check(std::all_of(first, first + 8, [](int16_t value) { return value == 0; }), "first capture was released before samples were written");
        check(std::all_of(second, second + 8, [](int16_t value) { return value == 0; }), "second capture was released before samples were written");
        // Once completion was published, a caller may immediately reuse its
        // arrays. Any later task write would fail the checks after task exit.
        std::fill_n(first, 8, int16_t(4321));
        std::fill_n(second, 8, int16_t(4321));
        microphone._task_running = false;
    };
    m5::Mic_Class::mic_task(&microphone);
    check(std::all_of(first, first + 8, [](int16_t value) { return value == 4321; }), "capture wrote the first released buffer again");
    check(std::all_of(second, second + 8, [](int16_t value) { return value == 4321; }), "capture wrote the second released buffer again");
    check(microphone.canRecord() && microphone.recordingQueueSize() == 0, "completed queue did not become available");
    microphone.end();
    checkEmpty(microphone);
}

void testSpeakerQueueEligibility() {
    reset();
    m5::Speaker_Class speaker;
    int16_t first[8]{}, second[8]{};
    check(speaker.canPlay(0) && speaker.isPlaying(uint8_t(0)) == 0, "idle speaker channel is not available");
    check(!speaker.canPlay(8) && speaker.isPlaying(uint8_t(8)) == 0, "invalid speaker channel was accepted");
    check(speaker._play_raw(first, 8, true, true, 16000, false, 1, 0, false, false), "first raw playback failed");
    check(speaker.isPlaying(uint8_t(0)) == 1 && !speaker.canPlay(0), "speaker pending-before-task slot could block despite count below two");
    __atomic_store_n(&speaker._ch_info[0].flip, true, __ATOMIC_RELEASE);
    check(speaker.canPlay(0), "speaker did not expose next available descriptor");
    check(speaker._play_raw(second, 8, true, true, 16000, false, 1, 0, false, false), "second raw playback failed");
    check(speaker.isPlaying(uint8_t(0)) == 2 && !speaker.canPlay(0), "speaker lost two queued buffer ownership markers");
    __atomic_store_n(&speaker._ch_info[0].wavinfo[0].repeat, uint32_t(0), __ATOMIC_RELEASE);
    check(speaker.isPlaying(uint8_t(0)) == 1 && !speaker.canPlay(0), "speaker count alone incorrectly exposed a still-busy target");
    __atomic_store_n(&speaker._ch_info[0].flip, false, __ATOMIC_RELEASE);
    check(speaker.canPlay(0), "speaker completion/advance did not expose the released descriptor");
    speaker.end();
}

void testInvalidI2sReadDoesNotProduceOldPcm() {
    for (bool failed_read : {false, true}) {
        reset();
        m5::Mic_Class microphone;
        int16_t recording[8];
        std::fill_n(recording, 8, int16_t(1234));
        microphone._task_running = true;
        microphone._rec_info[1] = {recording, 8, 0, false, true};
        sdk_test::runtime.on_read = [&] {
            if (sdk_test::runtime.read_calls == 3) {
                sdk_test::runtime.read_result_bytes = failed_read ? 16 : 0;
                sdk_test::runtime.read_result = failed_read ? -1 : ESP_OK;
            } else if (sdk_test::runtime.read_calls == 4) {
                check(std::all_of(recording, recording + 8, [](int16_t value) { return value == 1234; }),
                      "failed/empty I2S read produced stale PCM before valid input arrived");
                check(microphone.recordingQueueSize() == 1, "invalid read prematurely completed capture");
                sdk_test::runtime.read_result_bytes = 16;
                sdk_test::runtime.read_result = ESP_OK;
            }
        };
        sdk_test::runtime.on_wait = [&] { microphone._task_running = false; };
        m5::Mic_Class::mic_task(&microphone);
        check(sdk_test::runtime.read_calls == 4 && sdk_test::runtime.delay_calls == 1,
              "invalid I2S read did not yield and retry exactly once");
        check(std::all_of(recording, recording + 8, [](int16_t value) { return value == 0; }),
              "capture did not resume after valid I2S input arrived");
        microphone.end();
    }
}

void testOddDestinationLengthsStayInBounds() {
    for (bool sixteen_bit : {false, true}) {
        for (bool stereo_destination : {false, true}) {
            for (size_t length : {size_t(1), size_t(3), size_t(5), size_t(17)}) {
                reset();
                m5::Mic_Class microphone;
                auto recording16 = std::make_unique<int16_t[]>(length);
                auto recording8 = std::make_unique<uint8_t[]>(length);
                std::fill_n(recording16.get(), length, int16_t(1234));
                std::fill_n(recording8.get(), length, uint8_t(123));
                void* destination = sixteen_bit ? static_cast<void*>(recording16.get())
                                               : static_cast<void*>(recording8.get());
                microphone._task_running = true;
                microphone._rec_info[1] = {destination, length, 0, stereo_destination, sixteen_bit};
                sdk_test::runtime.on_wait = [&] { microphone._task_running = false; };
                m5::Mic_Class::mic_task(&microphone);
                check(microphone.recordingQueueSize() == 0, "odd capture did not publish completion");
                if (sixteen_bit) {
                    check(std::all_of(recording16.get(), recording16.get() + length, [](int16_t value) { return value == 0; }),
                          "odd 16-bit capture did not fill every destination sample");
                } else {
                    check(std::all_of(recording8.get(), recording8.get() + length, [](uint8_t value) { return value == 128; }),
                          "odd 8-bit capture did not fill every destination sample");
                }
                microphone.end();
            }
        }
    }
}

void testMicSemaphoreFailure() {
    reset();
    m5::Mic_Class microphone;
    sdk_test::runtime.fail_semaphore = true;
    check(!microphone.begin(), "Mic.begin accepted semaphore allocation failure");
    check(!microphone._task_running && !microphone._task_handle, "failed Mic.begin reported running");
    check(sdk_test::runtime.setups == 0 && sdk_test::runtime.enabled_calls == 0,
          "Mic.begin configured hardware after semaphore failure");
}

void testMicTaskFailure(bool pinned) {
    reset();
    m5::Mic_Class microphone;
    microphone._cfg.task_pinned_core = pinned ? 0 : UINT_MAX;
    sdk_test::runtime.fail_task = true;
    check(!microphone.begin(), "Mic.begin accepted task creation failure");
    check(!microphone._task_running && !microphone._task_handle, "Mic task failure left a running flag or handle");
    check(sdk_test::runtime.uninstalls == 1 && sdk_test::runtime.disabled_calls == 1,
          "Mic task failure leaked initialized I2S or codec enable state");
    check(pinned ? sdk_test::runtime.pinned_creations == 1 : sdk_test::runtime.plain_creations == 1,
          "test did not exercise the selected Mic task creation branch");
}

void testMicCallbackAndSetupFailure() {
    for (bool callback_failure : {true, false}) {
        reset();
        m5::Mic_Class microphone;
        sdk_test::runtime.callback_result = !callback_failure;
        sdk_test::runtime.setup_result = callback_failure ? ESP_OK : -1;
        check(!microphone.begin(), "Mic.begin accepted codec callback or I2S setup failure");
        check(!microphone._task_running && !microphone._task_handle, "Mic setup failure reported running");
        check(sdk_test::runtime.tasks.empty(), "Mic setup failure still created a capture task");
        check(sdk_test::runtime.disabled_calls == 1, "Mic callback/setup failure left codec enabled");
        check(sdk_test::runtime.uninstalls == (callback_failure ? 1u : 0u),
              "Mic failure did not release its own setup or uninstalled an unrelated owner");
        microphone.end();
    }
}

void testSpeakerSemaphoreFailure() {
    reset();
    m5::Speaker_Class speaker;
    sdk_test::runtime.fail_semaphore = true;
    check(!speaker.begin(), "Speaker.begin accepted semaphore allocation failure");
    check(!speaker._task_running && !speaker._task_handle, "failed Speaker.begin reported running");
    check(sdk_test::runtime.setups == 0, "speaker configured I2S after semaphore failure");
}

void testSpeakerTaskFailure(bool pinned) {
    reset();
    m5::Speaker_Class speaker;
    speaker._cfg.task_pinned_core = pinned ? 0 : UINT_MAX;
    sdk_test::runtime.fail_task = true;
    check(!speaker.begin(), "Speaker.begin accepted task creation failure");
    check(!speaker._task_running && !speaker._task_handle, "speaker task failure left running flag or handle");
    check(sdk_test::runtime.uninstalls == 1 && sdk_test::runtime.disabled_calls == 1,
          "speaker task failure leaked I2S or codec enable state");
    check(pinned ? sdk_test::runtime.pinned_creations == 1 : sdk_test::runtime.plain_creations == 1,
          "test did not exercise the selected Speaker task creation branch");
}

void testSpeakerCallbackAndSetupFailure() {
    for (bool callback_failure : {true, false}) {
        reset();
        m5::Speaker_Class speaker;
        sdk_test::runtime.callback_result = !callback_failure;
        sdk_test::runtime.setup_result = callback_failure ? ESP_OK : -1;
        check(!speaker.begin(), "Speaker.begin accepted callback or I2S setup failure");
        check(!speaker._task_running && !speaker._task_handle, "speaker setup failure reported running");
        check(sdk_test::runtime.tasks.empty(), "speaker setup failure still created a task");
        check(sdk_test::runtime.disabled_calls == 1, "Speaker callback/setup failure left codec enabled");
        check(sdk_test::runtime.uninstalls == (callback_failure ? 1u : 0u),
              "Speaker failure did not release its own setup or uninstalled an unrelated owner");
        speaker.end();
    }
}

void testRawPlaybackFailurePropagation() {
    for (unsigned failure = 0; failure < 4; ++failure) {
        reset();
        m5::Speaker_Class speaker;
        int16_t recording[8]{};
        sdk_test::runtime.fail_semaphore = failure == 0;
        sdk_test::runtime.fail_task = failure == 1;
        sdk_test::runtime.callback_result = failure != 2;
        sdk_test::runtime.setup_result = failure == 3 ? -1 : ESP_OK;
        check(!speaker._play_raw(recording, 8, true, true, 16000, false, 1, 0, false, false),
              "playRaw returned success after failed Speaker.begin");
        check(speaker._play_channel_bits.load() == 0, "failed playback left a queued channel bit");
        speaker.end();
    }
}

void testSpeakerEndClearsPlaybackDescriptors() {
    reset();
    m5::Speaker_Class speaker;
    int16_t recording[8]{};
    check(speaker._play_raw(recording, 8, true, true, 16000, false, 1, 0, false, false),
          "valid raw playback failed");
    check(speaker._play_channel_bits.load() == 1, "raw playback did not queue on channel zero");
    speaker.end();
    check(!speaker._task_running && !speaker._task_handle && speaker._play_channel_bits.load() == 0,
          "Speaker.end returned before clearing playback state");
    for (const auto& channel : speaker._ch_info) {
        for (const auto& wav : channel.wavinfo) {
            check(!wav.data && wav.length == 0 && wav.repeat == 0, "Speaker.end retained raw buffer ownership");
        }
    }
}

void testRawRecordingFailurePropagation() {
    reset();
    m5::Mic_Class microphone;
    int16_t recording[8]{};
    sdk_test::runtime.fail_task = true;
    check(!microphone._rec_raw(recording, 8, true, 16000, false),
          "record returned success after failed Mic.begin");
    checkEmpty(microphone);
    sdk_test::runtime.fail_task = false;
    check(microphone._rec_raw(recording, 8, true, 16000, false), "valid record request failed");
    check(microphone._rec_info[0].data == recording && microphone._rec_info[0].length == 8,
          "record request was not queued in the actual SDK descriptor");
    microphone.end();
    checkEmpty(microphone);
}
}

int main() {
    const struct { const char* name; void (*run)(); } tests[] = {
        {"idle Mic.end clears both descriptors and flags", testIdleEndClearsDescriptors},
        {"Mic.end joins, frees old arrays and reopens empty", testJoinThenReleaseAndReopen},
        {"actual microphone inner loop exits on cancellation", testCaptureLoopCancellation},
        {"actual queue APIs own queued-before-task capture through final PCM write", testRecordingQueueBeforeTaskAndAfterCompletion},
        {"actual speaker queue eligibility prevents transient blocking", testSpeakerQueueEligibility},
        {"failed/empty I2S reads yield without emitting stale PCM", testInvalidI2sReadDoesNotProduceOldPcm},
        {"odd mono/stereo destination lengths stay within 8/16-bit buffers", testOddDestinationLengthsStayInBounds},
        {"Mic semaphore allocation failure", testMicSemaphoreFailure},
        {"Mic plain task creation failure", [] { testMicTaskFailure(false); }},
        {"Mic pinned task creation failure", [] { testMicTaskFailure(true); }},
        {"Mic callback and I2S setup failures", testMicCallbackAndSetupFailure},
        {"Speaker semaphore allocation failure", testSpeakerSemaphoreFailure},
        {"Speaker plain task creation failure", [] { testSpeakerTaskFailure(false); }},
        {"Speaker pinned task creation failure", [] { testSpeakerTaskFailure(true); }},
        {"Speaker callback and I2S setup failures", testSpeakerCallbackAndSetupFailure},
        {"raw playback propagates all begin failures", testRawPlaybackFailurePropagation},
        {"Speaker.end joins and clears playback ownership", testSpeakerEndClearsPlaybackDescriptors},
        {"raw recording failure and descriptor cleanup", testRawRecordingFailurePropagation},
    };
    for (const auto& test : tests) {
        try { test.run(); std::cout << "PASS: " << test.name << '\n'; }
        catch (const std::exception& error) {
            std::cerr << "FAIL: " << test.name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "Actual extracted SDK lifecycle/capture methods passed ASan/UBSan host tests.\n";
}
