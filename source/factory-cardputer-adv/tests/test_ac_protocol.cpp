#include "apps/app_remote/ac_protocol.h"
#include "upstream_reference.h"

#include <array>
#include <cassert>
#include <iostream>

using factory_ir::AcProtocol;
using factory_ir::AcSettings;
using factory_ir::AcMode;
using factory_ir::AcFan;
using factory_ir::CoolixAction;
using factory_ir::Pulse;

static std::vector<upstream::Segment> normalize(const std::vector<Pulse>& pulses)
{
    upstream::IRsend normalized;
    for (const auto& pulse : pulses) {
        assert(pulse.markUs <= 32767 && pulse.spaceUs <= 32767);
        assert(pulse.markUs || pulse.spaceUs);
        normalized.mark(pulse.markUs);
        normalized.space(pulse.spaceUs);
    }
    return normalized.segments;
}

static void compare(const std::vector<upstream::Segment>& actual,
                    const std::vector<upstream::Segment>& reference)
{
    assert(actual.size() == reference.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        assert(actual[i].mark == reference[i].mark);
        assert(actual[i].duration == reference[i].duration);
    }
}

static uint64_t decodeFrame(const std::vector<upstream::Segment>& wave,
                            AcProtocol protocol, unsigned frame)
{
    // Independently decode 48 transmitted data bits after each header.
    assert(wave.size() == 200 || (protocol == AcProtocol::Coolix24 && wave.size() == 100));
    const size_t start = frame * 100;
    assert(start + 100 <= wave.size());
    const bool midea = protocol == AcProtocol::Midea48;
    const uint16_t mark = midea ? 560 : 552;
    const uint16_t one = midea ? 1680 : 1656;
    assert(wave[start].mark && wave[start].duration == (midea ? 4480 : 4692));
    assert(!wave[start + 1].mark && wave[start + 1].duration == (midea ? 4480 : 4416));
    uint64_t decoded = 0;
    for (unsigned bit = 0; bit < 48; ++bit) {
        const auto& m = wave[start + 2 + bit * 2];
        const auto& s = wave[start + 3 + bit * 2];
        assert(m.mark && m.duration == mark && !s.mark);
        assert(s.duration == mark || s.duration == one);
        decoded = (decoded << 1) | (s.duration == one);
    }
    assert(wave[start + 98].mark && wave[start + 98].duration == mark);
    assert(!wave[start + 99].mark);
    const uint32_t gap = midea ? 5600 : 5244;
    assert(wave[start + 99].duration == gap + (start + 100 == wave.size() ? 100000 : 0));
    return decoded;
}

static uint8_t reversed(uint8_t value)
{
    uint8_t result = 0;
    for (unsigned i = 0; i < 8; ++i) {
        result = static_cast<uint8_t>((result << 1) | (value & 1));
        value >>= 1;
    }
    return result;
}

static uint32_t decodeCoolix(uint64_t transmission)
{
    uint32_t state = 0;
    for (int shift = 32; shift >= 0; shift -= 16) {
        const uint8_t value = static_cast<uint8_t>(transmission >> (shift + 8));
        const uint8_t inverse = static_cast<uint8_t>(transmission >> shift);
        assert(static_cast<uint8_t>(value ^ inverse) == 0xffU);
        state = (state << 8) | value;
    }
    return state;
}

static uint64_t decodeBoschFrame(const std::vector<upstream::Segment>& wave, unsigned frame)
{
    assert(wave.size() == 200 || wave.size() == 300);
    const size_t start = frame * 100;
    assert(start + 100 <= wave.size());
    assert(wave[start].mark && wave[start].duration == 4366);
    assert(!wave[start + 1].mark && wave[start + 1].duration == 4415);
    uint64_t decoded = 0;
    for (unsigned bit = 0; bit < 48; ++bit) {
        const auto& m = wave[start + 2 + bit * 2];
        const auto& s = wave[start + 3 + bit * 2];
        assert(m.mark && m.duration == 456 && !s.mark);
        assert(s.duration == 610 || s.duration == 1645);
        decoded = (decoded << 1) | (s.duration == 1645);
    }
    assert(wave[start + 98].mark && wave[start + 98].duration == 456);
    assert(!wave[start + 99].mark);
    assert(wave[start + 99].duration == 5235 + (start + 100 == wave.size() ? 100000 : 0));
    return decoded;
}

static void checkChecksumVectors()
{
    // Independent known values from pinned upstream TestMideaACClass.Checksums.
    constexpr std::array<uint64_t, 8> inputs = {
        UINT64_C(0xa1826fffff62), UINT64_C(0xa18177ffff70),
        UINT64_C(0xa1826fffff00), UINT64_C(0xa18177ffff00),
        UINT64_C(0), UINT64_C(0x1234567890ab),
        UINT64_C(0xffffffffffff), UINT64_C(0xff1234567890ab)};
    constexpr std::array<uint8_t, 8> expected = {0x62, 0x70, 0x62, 0x70, 0, 0xdf, 0xa0, 0xdf};
    for (size_t i = 0; i < inputs.size(); ++i) {
        assert(factory_ir::detail::mideaChecksum(inputs[i]) == expected[i]);
        assert(upstream::IRMideaAC::calcChecksum(inputs[i]) == expected[i]);
    }
}

static void checkAllSettings()
{
    unsigned cases = 0;
    for (auto protocol : {AcProtocol::Midea48, AcProtocol::Coolix24}) {
        for (bool power : {false, true}) {
            for (uint8_t temperature = 17; temperature <= 30; ++temperature) {
                AcSettings settings{power, temperature};
                std::vector<Pulse> pulses{{123, 456}};
                std::string error = "stale error";
                assert(factory_ir::encodeAc(settings, protocol, pulses, &error));
                assert(error.empty() && pulses.size() == 103);
                const auto wave = normalize(pulses);
                uint64_t duration = 0;
                for (const auto& segment : wave) duration += segment.duration;
                upstream::IRsend reference;
                if (protocol == AcProtocol::Midea48) {
                    upstream::IRMideaAC ac;
                    ac.setUseCelsius(true);
                    ac.setMode(upstream::kMideaACCool);
                    ac.setFan(upstream::kMideaACFanAuto);
                    ac.setTemp(temperature, true);
                    ac.setPower(power);
                    const uint64_t state = ac.getRaw();
                    reference.sendMidea(state, 48, 0);
                    const uint64_t raw = decodeFrame(wave, protocol, 0);
                    assert(raw == state);
                    assert((raw ^ decodeFrame(wave, protocol, 1)) == UINT64_C(0xffffffffffff));
                    // Checksum: reversed bytes, including checksum, sum to zero.
                    unsigned sum = 0;
                    for (unsigned byte = 0; byte < 6; ++byte) sum += reversed(static_cast<uint8_t>(raw >> (8 * byte)));
                    assert((sum & 0xffU) == 0);
                    assert(((raw >> 39) & 1) == power);
                    assert(((raw >> 32) & 0x1fU) == 0);  // Cool, fan auto.
                    assert(((raw >> 24) & 0x3fU) == temperature - 17);  // Native Celsius.
                    assert(duration == 291520);
                } else {
                    upstream::IRCoolixAC ac;
                    ac.setMode(upstream::kCoolixCool);
                    ac.setFan(upstream::kCoolixFanAuto);
                    ac.setTemp(temperature);
                    ac.setPower(power);
                    const uint32_t state = ac.getRaw();
                    reference.sendCOOLIX(state, 24, 1);
                    assert(decodeCoolix(decodeFrame(wave, protocol, 0)) == state);
                    assert(decodeCoolix(decodeFrame(wave, protocol, 1)) == state);
                    if (!power) assert(state == 0xb27be0U);
                    else {
                        assert(((state >> 13) & 7) == 5);  // Automatic fan.
                        assert(((state >> 8) & 31) == 31);  // Sensor ignored.
                        assert((state & 0xfU) == 0);  // Cool, no Zone Follow.
                        if (temperature == 18) assert(state == 0xb2bf10U);  // Upstream real vector.
                        if (temperature == 24) assert(state == 0xb2bf40U);  // Upstream Issue624 vector.
                        if (temperature == 26) assert(state == 0xb2bfd0U);
                    }
                    assert(duration == 288784);
                }
                assert(reference.carrier == factory_ir::kAcCarrierHz);
                compare(wave, reference.segments);
                ++cases;
            }
        }
    }
    std::cout << "Compared " << cases << " complete waveforms with actual upstream setters and send loops\n";
}

static void checkBoschSettings()
{
    unsigned cases = 0;
    for (bool power : {false, true}) {
        for (uint8_t temperature = 16; temperature <= 30; ++temperature) {
            std::vector<Pulse> pulses{{123, 456}};
            std::string error = "stale error";
            assert(factory_ir::encodeAc({power, temperature}, AcProtocol::Bosch144, pulses, &error));
            assert(error.empty() && pulses.size() == (power ? 153 : 103));
            const auto wave = normalize(pulses);
            upstream::IRBosch144AC ac;
            ac.setMode(upstream::kBosch144Cool);
            ac.setFan(upstream::kBosch144FanAuto);
            ac.setTemp(temperature, false);
            ac.setPower(power);
            upstream::IRsend reference;
            reference.sendBosch144(power ? ac.getRaw() : upstream::kBosch144Off, power ? 18 : 12, 0);
            assert(reference.carrier == factory_ir::kAcCarrierHz);
            assert(reference.duty == factory_ir::kAcCarrierDutyPercent);
            compare(wave, reference.segments);
            const auto first = decodeBoschFrame(wave, 0);
            const auto second = decodeBoschFrame(wave, 1);
            assert(first == second);
            if (!power) {
                assert(first == UINT64_C(0xb24d7b84e01f));
                // The decoded OFF payload is exactly Coolix OFF, but Bosch
                // has its own upstream mark/space timings and zero repeats.
                assert(decodeCoolix(first) == 0xb27be0U);
            } else {
                const auto third = decodeBoschFrame(wave, 2);
                unsigned sum = 0;
                for (unsigned shift = 8; shift <= 40; shift += 8) sum += static_cast<uint8_t>(third >> shift);
                assert((sum & 0xffU) == (third & 0xffU));
                assert((third >> 32) == UINT64_C(0xd566));  // Cool, automatic fan.
                if (temperature == 16) {
                    assert(first == UINT64_C(0xb24dbf4000ff));
                    assert(third == UINT64_C(0xd5660010004b));
                } else if (temperature == 17) {
                    assert(first == UINT64_C(0xb24dbf4000ff));
                    assert(third == UINT64_C(0xd5660000003b));
                } else if (temperature == 26) {
                    // Independently reconstructed wire bytes from the RN02S13
                    // logic-analyser capture author's source. See provenance.
                    // Its first two sections are Coolix-like; it also emits
                    // this third 48-bit section, absent from our A/B candidates.
                    assert(first == UINT64_C(0xb24dbf40d02f));
                    assert(third == UINT64_C(0xd5660000003b));
                    assert(decodeCoolix(first) == 0xb2bfd0U);
                }
            }
            ++cases;
        }
    }
    std::cout << "Compared " << cases << " Bosch144 ON/OFF complete waveforms; verified third-section checksum and RN02S13 26C capture facts\n";
}

static uint8_t nativeMode(AcMode mode)
{
    switch (mode) {
        case AcMode::Cool: return upstream::kCoolixCool;
        case AcMode::Dry: return upstream::kCoolixDry;
        case AcMode::Auto: return upstream::kCoolixAuto;
        case AcMode::Heat: return upstream::kCoolixHeat;
        case AcMode::Fan: return upstream::kCoolixFan;
    }
    assert(false);
    return 0;
}

static uint8_t nativeFan(AcFan fan)
{
    switch (fan) {
        case AcFan::Auto: return upstream::kCoolixFanAuto;
        case AcFan::Low: return upstream::kCoolixFanMin;
        case AcFan::Medium: return upstream::kCoolixFanMed;
        case AcFan::High: return upstream::kCoolixFanMax;
    }
    assert(false);
    return 0;
}

static void checkFullCoolix()
{
    unsigned cases = 0;
    for (bool power : {false, true}) {
        for (auto mode : {AcMode::Cool, AcMode::Dry, AcMode::Auto, AcMode::Heat, AcMode::Fan}) {
            for (auto fan : {AcFan::Auto, AcFan::Low, AcFan::Medium, AcFan::High}) {
                for (uint8_t temperature = 17; temperature <= 30; ++temperature) {
                    const AcSettings settings{power, temperature, mode, fan};
                    std::vector<Pulse> pulses{{123, 456}};
                    std::string error = "stale";
                    assert(factory_ir::encodeAc(settings, AcProtocol::Coolix24, pulses, &error));
                    assert(error.empty() && pulses.size() == 103);
                    upstream::IRCoolixAC reference;
                    // Official IRac::coolix sets temperature BEFORE mode,
                    // because Fan mode uses a synthetic temperature value.
                    reference.setTemp(temperature);
                    reference.setMode(nativeMode(mode));
                    reference.setFan(nativeFan(fan));
                    reference.setPower(power);
                    const uint32_t state = reference.getRaw();
                    if (power) {
                        assert(reference.getMode() == nativeMode(mode));
                        if (mode != AcMode::Fan) assert(reference.getTemp() == temperature);
                    }
                    reference.send();  // Actual upstream default-repeat implementation.
                    const auto wave = normalize(pulses);
                    compare(wave, reference._irsend.segments);
                    assert(reference._irsend.carrier == factory_ir::kAcCarrierHz);
                    assert(reference._irsend.duty == factory_ir::kAcCarrierDutyPercent);
                    assert(decodeCoolix(decodeFrame(wave, AcProtocol::Coolix24, 0)) == state);
                    assert(decodeCoolix(decodeFrame(wave, AcProtocol::Coolix24, 1)) == state);
                    if (!power) assert(state == 0xb27be0U);
                    else {
                        uint8_t expectedFan = nativeFan(fan);
                        if (fan == AcFan::Auto && (mode == AcMode::Auto || mode == AcMode::Dry)) expectedFan = 0;
                        assert(((state >> 13) & 7) == expectedFan);
                        assert(((state >> 8) & 31) == 31);  // Sensor ignored.
                        if (mode == AcMode::Fan) {
                            assert((state & 0xfcU) == 0xe4U);  // Synthetic Dry + 14 temp code.
                            if (fan == AcFan::Auto) assert(state == 0xb2bfe4U);  // Upstream known Fan vector.
                        } else assert(((state >> 2) & 3U) == nativeMode(mode));
                        if (temperature == 26 && mode == AcMode::Cool && fan == AcFan::Auto)
                            assert(state == 0xb2bfd0U);  // Accepted original B payload unchanged.
                    }
                    ++cases;
                }
            }
        }
    }
    std::cout << "Compared " << cases << " full Coolix settings waveforms (5 modes x 4 fans x 14 temperatures x power), including Fan synthetic code\n";
}

static void checkCoolixActions()
{
    constexpr std::array<CoolixAction, 6> actions = {CoolixAction::Swing, CoolixAction::SwingStep,
        CoolixAction::Sleep, CoolixAction::Turbo, CoolixAction::Led, CoolixAction::Clean};
    using Setter = void (upstream::IRCoolixAC::*)();
    constexpr std::array<Setter, 6> setters = {&upstream::IRCoolixAC::setSwing,
        &upstream::IRCoolixAC::setSwingVStep, &upstream::IRCoolixAC::setSleep,
        &upstream::IRCoolixAC::setTurbo, &upstream::IRCoolixAC::setLed, &upstream::IRCoolixAC::setClean};
    constexpr std::array<uint32_t, 6> expected = {0xb26be0U, 0xb20fe0U, 0xb2e003U,
        0xb5f5a2U, 0xb5f5a5U, 0xb5f5aaU};
    unsigned cases = 0;
    for (size_t i = 0; i < actions.size(); ++i) {
        upstream::IRCoolixAC reference;
        for (unsigned press = 0; press < 2; ++press) {
            std::vector<Pulse> pulses{{123, 456}};
            std::string error = "stale";
            assert(factory_ir::encodeCoolixAction(actions[i], pulses, &error));
            assert(error.empty());
            const bool step = actions[i] == CoolixAction::SwingStep;
            assert(pulses.size() == (step ? 53 : 103));
            reference._irsend.segments.clear();
            (reference.*setters[i])();
            assert(reference.getRaw() == expected[i]);
            reference.send();
            const auto wave = normalize(pulses);
            compare(wave, reference._irsend.segments);
            assert(reference._irsend.carrier == factory_ir::kAcCarrierHz);
            assert(reference._irsend.duty == factory_ir::kAcCarrierDutyPercent);
            assert(decodeCoolix(decodeFrame(wave, AcProtocol::Coolix24, 0)) == expected[i]);
            if (!step) assert(decodeCoolix(decodeFrame(wave, AcProtocol::Coolix24, 1)) == expected[i]);
            // Same command is emitted for a second press; there is no absolute
            // on/off state bit or automatic normal-state/power-on transmission.
            assert(wave.size() == (step ? 100 : 200));
            ++cases;
        }
    }
    // Upstream's SwingH hexadecimal COMMENT is wrong: the binary constant
    // is B2F5A2, whereas Turbo is B5F5A2. No unverified SwingH API is exposed.
    static_assert(upstream::kCoolixSwingH == 0xb2f5a2U, "Check actual binary constant");
    static_assert(upstream::kCoolixSwingH != upstream::kCoolixTurbo, "Not an actual value collision");
    std::cout << "Compared " << cases << " actual Coolix action sends (6 actions x 2 presses), including single-frame SwingStep\n";
}

static void checkInvalidInputs()
{
    unsigned cases = 0;
    for (auto protocol : {AcProtocol::Midea48, AcProtocol::Coolix24, AcProtocol::Bosch144}) {
        for (bool power : {false, true}) {
            for (unsigned temperature = 0; temperature <= 255; ++temperature) {
                const unsigned minimum = protocol == AcProtocol::Bosch144 ? 16 : 17;
                if (temperature >= minimum && temperature <= 30) continue;
                std::vector<Pulse> pulses{{123, 456}};
                std::string error;
                assert(!factory_ir::encodeAc({power, static_cast<uint8_t>(temperature)}, protocol, pulses, &error));
                assert(pulses.empty() && !error.empty());
                assert(!factory_ir::encodeAc({power, static_cast<uint8_t>(temperature)}, protocol, pulses));
                ++cases;
            }
        }
    }
    for (unsigned value = 3; value <= 255; ++value) {
        std::vector<Pulse> pulses{{123, 456}};
        std::string error;
        assert(!factory_ir::encodeAc({}, static_cast<AcProtocol>(value), pulses, &error));
        assert(pulses.empty() && !error.empty());
        ++cases;
    }
    for (auto protocol : {AcProtocol::Midea48, AcProtocol::Coolix24, AcProtocol::Bosch144}) {
        for (bool power : {false, true}) {
            for (unsigned value = 5; value <= 255; ++value) {
                std::vector<Pulse> pulses{{123, 456}};
                std::string error;
                assert(!factory_ir::encodeAc({power, 26, static_cast<AcMode>(value), AcFan::Auto}, protocol, pulses, &error));
                assert(pulses.empty() && !error.empty());
                ++cases;
            }
            for (unsigned value = 4; value <= 255; ++value) {
                std::vector<Pulse> pulses{{123, 456}};
                std::string error;
                assert(!factory_ir::encodeAc({power, 26, AcMode::Cool, static_cast<AcFan>(value)}, protocol, pulses, &error));
                assert(pulses.empty() && !error.empty());
                ++cases;
            }
        }
    }
    for (auto protocol : {AcProtocol::Midea48, AcProtocol::Bosch144}) {
        for (bool power : {false, true}) {
            for (auto mode : {AcMode::Cool, AcMode::Dry, AcMode::Auto, AcMode::Heat, AcMode::Fan}) {
                for (auto fan : {AcFan::Auto, AcFan::Low, AcFan::Medium, AcFan::High}) {
                    if (mode == AcMode::Cool && fan == AcFan::Auto) continue;
                    std::vector<Pulse> pulses{{123, 456}};
                    std::string error;
                    assert(!factory_ir::encodeAc({power, 26, mode, fan}, protocol, pulses, &error));
                    assert(pulses.empty() && !error.empty());
                    ++cases;
                }
            }
        }
    }
    for (unsigned value = 6; value <= 255; ++value) {
        std::vector<Pulse> pulses{{123, 456}};
        std::string error;
        assert(!factory_ir::encodeCoolixAction(static_cast<CoolixAction>(value), pulses, &error));
        assert(pulses.empty() && !error.empty());
        assert(!factory_ir::encodeCoolixAction(static_cast<CoolixAction>(value), pulses));
        ++cases;
    }
    std::cout << "Rejected " << cases << " invalid settings without retaining stale transmission\n";
}

int main()
{
    static_assert(sizeof(Pulse) == 4, "Protocol storage should stay bounded");
    static_assert(static_cast<uint8_t>(AcProtocol::Midea48) == 0, "Keep candidate A ABI");
    static_assert(static_cast<uint8_t>(AcProtocol::Coolix24) == 1, "Keep candidate B ABI");
    checkChecksumVectors();
    checkAllSettings();
    checkBoschSettings();
    checkFullCoolix();
    checkCoolixActions();
    checkInvalidInputs();
    std::cout << "PASS: pulse timing, inverted frames/bytes, temperature boundaries, power-off, checksum, repeat, 15-bit splitting\n";
}
