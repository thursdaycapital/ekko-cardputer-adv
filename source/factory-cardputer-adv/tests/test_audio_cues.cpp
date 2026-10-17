#include <apps/utils/audio/audio.h>
#include <hal/hal.h>
#include <cmath>
#include <iostream>
#include <limits>

static CueHal hal;
CueHal& GetHAL() { return hal; }
static size_t checks = 0;
static void pass(const char* label) { ++checks; std::cout << "PASS " << label << '\n'; }
static std::vector<int16_t> legacyTone(int frequency, size_t frames) {
    std::vector<int16_t> result(frames * 2);
    const float amplitude = 32767.0f / 5;
    for (size_t i = 0; i < frames; ++i) {
        float amp = amplitude;
        if (frames - i <= 200) amp *= static_cast<float>(frames - i) / 200;
        const int16_t value = static_cast<int16_t>(amp * std::sin(2.0 * M_PI * frequency * i / 48000));
        result[i * 2] = result[i * 2 + 1] = value;
    }
    return result;
}
int main() {
    audio::play_tone(440, .02);
    assert(hal.speaker.pending.size() == 1);
    // Consume only after play_tone returns: ASan detects the old local-vector
    // lifetime bug here, rather than accepting synchronous copying as proof.
    hal.speaker.touchPending();
    assert(hal.speaker.pending[0].original == legacyTone(440, 960));
    assert(hal.speaker.pending[0].rate == 44100 && !hal.speaker.pending[0].stereo);
    pass("returned tone PCM stays alive with bit-identical factory waveform/rate/mode");
    audio::play_tone(660, .02);
    assert(hal.speaker.pending.size() == 2);
    assert(hal.speaker.pending[0].channel != hal.speaker.pending[1].channel);
    const size_t calls = hal.speaker.calls;
    for (int i = 0; i < 1000; ++i) audio::play_tone(220 + i, .02);
    assert(hal.speaker.calls == calls && hal.speaker.pending.size() == 2);
    hal.speaker.touchPending();
    pass("two independent live buffers; a 1000-key burst drops new cues without overwriting");
    hal.speaker.consumeOne();
    audio::play_tone(880, .03);
    assert(hal.speaker.pending.size() == 2);
    hal.speaker.touchPending();
    hal.speaker.end();
    pass("completed channel can resize/reuse while another channel still consumes old PCM");

    audio::play_melody({60, -1, 64}, .02);
    assert(hal.speaker.pending.size() == 1);
    const auto melody = hal.speaker.consumeOne();
    assert(melody.samples == 5760 && melody.rate == 44100 && !melody.stereo);
    assert(std::all_of(melody.original.begin() + 1920, melody.original.begin() + 3840,
                       [](int16_t value) { return value == 0; }));
    for (size_t i = 0; i < melody.original.size(); i += 2) assert(melody.original[i] == melody.original[i + 1]);
    pass("returned melody PCM, duplicated waveform and silence/rest timing remain valid");

    const size_t before = hal.speaker.calls;
    audio::play_tone(440, -1); audio::play_tone(440, 0);
    audio::play_tone(440, std::numeric_limits<double>::quiet_NaN());
    audio::play_tone(440, std::numeric_limits<double>::infinity());
    audio::play_tone(440, 1e300); audio::play_tone(440, 100);
    audio::play_tone(-1, .02);
    audio::play_melody({}, .02); audio::play_melody({60}, -1);
    audio::play_melody({std::numeric_limits<int>::max()}, .02);
    audio::play_melody(std::vector<int>(10000, 60), .02);
    audio::play_tone_from_midi(std::numeric_limits<int>::min(), .02);
    audio::play_tone_from_midi(std::numeric_limits<int>::max(), .02);
    audio::play_random_tone(std::numeric_limits<int>::max());
    audio::play_random_tone(std::numeric_limits<int>::min());
    hal.speaker.configuration.sample_rate = 0; audio::play_tone(440, .02);
    hal.speaker.configuration.sample_rate = 48000;
    assert(hal.speaker.calls == before && hal.speaker.pending.empty());
    pass("negative/zero/NaN/overflow durations, invalid MIDI/rate, oversize cues rejected safely");

    audio::play_tone(440, 1.0 / 48000);
    assert(hal.speaker.pending.size() == 1 && hal.speaker.pending[0].samples == 2);
    hal.speaker.end();
    audio::play_melody({60}, 1.0 / 48000);
    assert(hal.speaker.pending.size() == 1 && hal.speaker.pending[0].samples == 2);
    hal.speaker.end();
    pass("shorter-than-fade and single-frame cues avoid unsigned underflow");

    audio::set_keyboard_sfx_enable(false);
    audio::play_tone(440, .15);
    audio::play_tone(660, .02);
    assert(hal.speaker.pending.size() == 2);
    hal.speaker.consumeOne();
    const size_t budgetCalls = hal.speaker.calls;
    audio::play_tone(880, .17);
    assert(hal.speaker.calls == budgetCalls && hal.speaker.pending.size() == 1);
    hal.speaker.touchPending();
    hal.speaker.end();
    audio::play_tone(880, .17);
    assert(hal.speaker.pending.size() == 1 && hal.speaker.pending[0].samples == 16320);
    hal.speaker.end();
    audio::set_keyboard_sfx_enable(false);
    pass("combined PCM budget drops oversize overlap and reclaims idle capacity after cancellation");

    audio::set_keyboard_sfx_enable(true);
    const size_t enabledSlots = hal.keyboard.onKeyEvent.slots.size();
    assert(enabledSlots == 1);
    audio::set_keyboard_sfx_enable(true); assert(hal.keyboard.onKeyEvent.slots.size() == enabledSlots);
    hal.keyboard.onKeyEvent.emit({true, KEY_1});
    assert(hal.speaker.pending.size() == 1 && hal.speaker.getVolume() == audio::DEFAULT_VOLUME);
    audio::set_keyboard_sfx_enable(false);
    assert(hal.keyboard.onKeyEvent.slots.empty());
    hal.speaker.touchPending();
    hal.speaker.end();
    audio::set_keyboard_sfx_enable(true);
    hal.keyboard.onKeyEvent.emit({true, KEY_2});
    assert(hal.speaker.pending.size() == 1);
    hal.speaker.end();
    pass("disabling callbacks retains live PCM; Speaker.end plus re-enable permits safe reclaim/reuse");

    audio::set_quiet_mode(true);
    hal.keyboard.onKeyEvent.emit({true, KEY_1}); audio::play_random_tone();
    assert(hal.speaker.pending.empty());
    audio::set_quiet_mode(false);
    hal.speaker.setVolume(0); audio::play_tone(440, .02); audio::play_melody({60}, .02);
    assert(hal.speaker.pending.empty());
    hal.speaker.setVolume(90);
    hal.speaker.allow_play = false; audio::play_tone(440, .02);
    assert(hal.speaker.pending.empty());
    hal.speaker.allow_play = true; hal.speaker.fail_play = true; audio::play_tone(440, .02);
    assert(hal.speaker.pending.empty());
    hal.speaker.fail_play = false; audio::play_tone(440, .02);
    assert(hal.speaker.pending.size() == 1); hal.speaker.end();
    audio::set_keyboard_sfx_enable(false);
    pass("quiet/mute, unavailable queue and failed play do not break later sounds");
    std::cout << checks << " production audio cue checks passed\n";
}
