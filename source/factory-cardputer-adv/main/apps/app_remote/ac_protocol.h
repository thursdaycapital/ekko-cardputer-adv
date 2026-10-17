// SPDX-License-Identifier: LGPL-2.1-only
// Midea / Coolix / Bosch144 protocol adaptation from IRremoteESP8266.
// Copyright 2017 bwze, crankyoldgit (Midea)
// Copyright bakrus; Copyright 2017,2019 David Conran (Coolix)
// Copyright 2022 David Conran, Nico Thien (Bosch)
// Copyright 2026 Cardputer-Adv adaptation contributors.
// Modified 2026-10-17: portable encoders and full Coolix state/action controls.
// See third_party/irremoteesp8266-reference/README.md and LICENSE.txt.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace factory_ir {

enum class AcProtocol : uint8_t { Midea48 = 0, Coolix24 = 1, Bosch144 = 2 };
enum class AcMode : uint8_t { Cool, Dry, Auto, Heat, Fan };
enum class AcFan : uint8_t { Auto, Low, Medium, High };
enum class CoolixAction : uint8_t { Swing, SwingStep, Sleep, Turbo, Led, Clean };

// A full normal state is generated for every send. Coolix supports all modes
// below; legacy Midea/Bosch candidates remain restricted to Cool + Auto fan.
struct AcSettings {
    bool power = true;
    uint8_t temperature = 26;
    AcMode mode = AcMode::Cool;
    AcFan fan = AcFan::Auto;
};

struct Pulse {
    uint16_t markUs;
    uint16_t spaceUs;
};

constexpr uint32_t kAcCarrierHz = 38000;
constexpr uint8_t kAcCarrierDutyPercent = 50;
constexpr uint8_t kAcMinimumTemperature = 17;
constexpr uint8_t kAcMaximumTemperature = 30;
constexpr uint8_t kBoschMinimumTemperature = 16;

namespace detail {

inline uint8_t reverseByte(uint8_t value)
{
    value = static_cast<uint8_t>((value >> 4) | (value << 4));
    value = static_cast<uint8_t>(((value & 0xccU) >> 2) | ((value & 0x33U) << 2));
    return static_cast<uint8_t>(((value & 0xaaU) >> 1) | ((value & 0x55U) << 1));
}

inline uint8_t mideaChecksum(uint64_t state)
{
    uint8_t sum = 0;
    for (unsigned i = 0; i < 5; ++i) {
        state >>= 8;
        sum = static_cast<uint8_t>(sum + reverseByte(static_cast<uint8_t>(state)));
    }
    return reverseByte(static_cast<uint8_t>(0U - sum));
}

inline uint64_t mideaState(const AcSettings& settings)
{
    // Equivalent to IRMideaAC::stateReset(), setUseCelsius(true),
    // setMode(kMideaACCool), setFan(kMideaACFanAuto), setTemp(temp, true),
    // and setPower(power). Preserve the default reserved bits and timers.
    uint64_t state = UINT64_C(0xa10040ffff00);
    if (settings.power) state |= UINT64_C(1) << 39;
    state |= static_cast<uint64_t>(settings.temperature - kAcMinimumTemperature) << 24;
    return state | mideaChecksum(state);
}

inline uint32_t coolixState(const AcSettings& settings)
{
    if (!settings.power) return 0xb27be0U;  // Upstream kCoolixOff command.
    constexpr uint8_t temperatures[] = {0, 1, 3, 2, 6, 7, 5, 4, 12, 13, 9, 8, 10, 11};
    constexpr uint8_t fans[] = {5, 4, 2, 1};
    const bool fanOnly = settings.mode == AcMode::Fan;
    const uint8_t mode = fanOnly ? 1 : static_cast<uint8_t>(settings.mode);
    uint8_t fan = fans[static_cast<uint8_t>(settings.fan)];
    // Actual upstream setFan(modecheck=true) maps only automatic speed to
    // Auto0 in Auto/Dry; it still accepts explicit low/medium/high speeds.
    if (settings.fan == AcFan::Auto &&
        (settings.mode == AcMode::Auto || settings.mode == AcMode::Dry)) fan = 0;
    const uint8_t temperature = fanOnly ? 14 :
        temperatures[settings.temperature - kAcMinimumTemperature];
    // Equivalent to stateReset -> setTemp -> setMode -> setFan -> setPower.
    // Mode MUST follow temperature: Fan is encoded as Dry + special temp=14.
    // Sensor ignored (31), Zone Follow off, reserved default prefix retained.
    return 0xb21f00U | (static_cast<uint32_t>(fan) << 13) |
           (static_cast<uint32_t>(temperature) << 4) | (static_cast<uint32_t>(mode) << 2);
}

inline std::array<uint8_t, 18> boschState(const AcSettings& settings)
{
    // IRBosch144AC native Celsius temperature codes. The first two sections
    // resemble Coolix, but the third carries additional mode/fan/temp state.
    constexpr uint8_t temperatures[] = {2, 0, 4, 12, 8, 24, 28, 20, 16, 48, 52, 36, 32, 40, 44};
    const uint8_t code = temperatures[settings.temperature - kBoschMinimumTemperature];
    std::array<uint8_t, 18> state{};
    for (unsigned offset : {0U, 6U}) {
        state[offset] = 0xb2;
        state[offset + 1] = 0x4d;
        state[offset + 2] = 0xbf;  // Auto fan (5), reserved low bits retained.
        state[offset + 3] = 0x40;
        state[offset + 4] = static_cast<uint8_t>((code >> 2) << 4);  // Cool = 0.
        state[offset + 5] = static_cast<uint8_t>(state[offset + 4] ^ 0xffU);
    }
    state[12] = 0xd5;
    state[13] = 0x66;  // Cool mode bit 0, auto fan low six bits = 51.
    state[14] = static_cast<uint8_t>((code & 1U) << 5);
    state[15] = static_cast<uint8_t>(((code >> 1) & 1U) << 4);  // Celsius.
    // Bytes 14, 15, 16 otherwise retain the upstream default disabled flags.
    for (unsigned i = 12; i < 17; ++i) state[17] = static_cast<uint8_t>(state[17] + state[i]);
    return state;
}

inline void appendPulse(std::vector<Pulse>& out, uint16_t mark, uint32_t space)
{
    // RMT uses 15-bit durations. Continuations are spaces without a mark;
    // transport must omit zero durations, keeping adjacent spaces continuous.
    constexpr uint32_t maximum = 32767;
    const uint16_t firstSpace = static_cast<uint16_t>(space > maximum ? maximum : space);
    out.push_back({mark, firstSpace});
    space -= firstSpace;
    while (space) {
        const uint16_t duration = static_cast<uint16_t>(space > maximum ? maximum : space);
        out.push_back({0, duration});
        space -= duration;
    }
}

inline void appendBits(std::vector<Pulse>& out, uint64_t value, unsigned bits,
                       uint16_t mark, uint16_t oneSpace, uint16_t zeroSpace)
{
    for (unsigned bit = bits; bit > 0; --bit) {
        appendPulse(out, mark, (value & (UINT64_C(1) << (bit - 1))) ? oneSpace : zeroSpace);
    }
}

inline void appendCoolix(std::vector<Pulse>& out, uint32_t state, unsigned frames)
{
    for (unsigned frame = 0; frame < frames; ++frame) {
        appendPulse(out, 4692, 4416);
        for (int shift = 16; shift >= 0; shift -= 8) {
            const uint8_t byte = static_cast<uint8_t>(state >> shift);
            appendBits(out, byte, 8, 552, 1656, 552);
            appendBits(out, static_cast<uint8_t>(byte ^ 0xffU), 8, 552, 1656, 552);
        }
        appendPulse(out, 552, frame + 1 == frames ? 105244U : 5244U);
    }
}

}  // namespace detail

// These are candidate protocols, not an assertion that a particular appliance
// understands them. Caller decides when to transmit; encoding never emits IR.
// Output is replaced, including on failure, so a failed request cannot resend
// a previous command accidentally. Midea/Coolix and Bosch-off contain 103
// pulses; Bosch-on contains 153. This does not confirm appliance compatibility.
inline bool encodeAc(const AcSettings& settings, AcProtocol protocol,
                     std::vector<Pulse>& out, std::string* error = nullptr)
{
    out.clear();
    if (error) error->clear();
    if (protocol != AcProtocol::Midea48 && protocol != AcProtocol::Coolix24 && protocol != AcProtocol::Bosch144) {
        if (error) *error = "Unsupported AC protocol";
        return false;
    }
    if (static_cast<uint8_t>(settings.mode) > static_cast<uint8_t>(AcMode::Fan) ||
        static_cast<uint8_t>(settings.fan) > static_cast<uint8_t>(AcFan::High)) {
        if (error) *error = "Unsupported AC mode or fan speed";
        return false;
    }
    if (protocol != AcProtocol::Coolix24 &&
        (settings.mode != AcMode::Cool || settings.fan != AcFan::Auto)) {
        if (error) *error = "Legacy candidate supports cooling with automatic fan only";
        return false;
    }
    const uint8_t minimum = protocol == AcProtocol::Bosch144 ? kBoschMinimumTemperature : kAcMinimumTemperature;
    if (settings.temperature < minimum || settings.temperature > kAcMaximumTemperature) {
        if (error) *error = protocol == AcProtocol::Bosch144 ?
            "Temperature must be between 16 and 30 Celsius" : "Temperature must be between 17 and 30 Celsius";
        return false;
    }
    out.reserve(protocol == AcProtocol::Bosch144 && settings.power ? 153 : 103);
    if (protocol == AcProtocol::Midea48) {
        const uint64_t state = detail::mideaState(settings);
        // Upstream minimum repeat is zero: one payload and its full inverse.
        for (unsigned phase = 0; phase < 2; ++phase) {
            detail::appendPulse(out, 4480, 4480);
            detail::appendBits(out, phase ? ~state : state, 48, 560, 1680, 560);
            detail::appendPulse(out, 560, phase == 1 ? 105600U : 5600U);
        }
    } else if (protocol == AcProtocol::Coolix24) {
        // Upstream default repeat is one: two frames, each byte followed by
        // its inverse. Do not confuse this with the Midea full-frame inverse.
        detail::appendCoolix(out, detail::coolixState(settings), 2);
    } else {
        // Bosch144 follows the upstream default of zero repeats. Power-on is
        // three 48-bit sections; power-off is the separate 96-bit command.
        const auto state = detail::boschState(settings);
        constexpr uint8_t off[] = {0xb2, 0x4d, 0x7b, 0x84, 0xe0, 0x1f,
                                   0xb2, 0x4d, 0x7b, 0x84, 0xe0, 0x1f};
        const uint8_t* bytes = settings.power ? state.data() : off;
        const unsigned sections = settings.power ? 3 : 2;
        for (unsigned section = 0; section < sections; ++section) {
            detail::appendPulse(out, 4366, 4415);
            for (unsigned byte = 0; byte < 6; ++byte) {
                detail::appendBits(out, bytes[section * 6 + byte], 8, 456, 1645, 610);
            }
            detail::appendPulse(out, 456, section + 1 == sections ? 105235U : 5235U);
        }
    }
    return true;
}

// A single user action. These are independent toggles/steps, with no appliance
// state acknowledgement. Caller must not present them as known ON/OFF states.
// The appliance normally needs to be powered on before receiving these actions;
// this encoder deliberately does not send an implicit power-on command.
inline bool encodeCoolixAction(CoolixAction action, std::vector<Pulse>& out,
                               std::string* error = nullptr)
{
    out.clear();
    if (error) error->clear();
    constexpr uint32_t states[] = {0xb26be0U, 0xb20fe0U, 0xb2e003U,
                                   0xb5f5a2U, 0xb5f5a5U, 0xb5f5aaU};
    const uint8_t index = static_cast<uint8_t>(action);
    if (index >= sizeof(states) / sizeof(states[0])) {
        if (error) *error = "Unsupported Coolix action";
        return false;
    }
    // Actual IRCoolixAC::send() suppresses the usual repeat for SwingVStep.
    const unsigned frames = action == CoolixAction::SwingStep ? 1 : 2;
    out.reserve(frames * 50 + 3);
    detail::appendCoolix(out, states[index], frames);
    return true;
}

}  // namespace factory_ir
