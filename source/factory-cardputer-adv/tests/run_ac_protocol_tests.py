#!/usr/bin/env python3
"""Compare the production encoder with pinned upstream setters and send methods.

The reference functions are extracted verbatim from the vendored upstream
sources into a temporary translation unit. Only GPIO timing is mocked; the
upstream payload fields, checksum, temperature map, and send loops are real.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

factory = Path(__file__).resolve().parent.parent
reference = factory / "third_party/irremoteesp8266-reference"
production = factory / "main/apps/app_remote/ac_protocol.h"


def function(text, name, overload=0):
    matches = list(re.finditer(r"^[\w][^\n]*\b" + re.escape(name) + r"\(", text, re.M))
    if len(matches) <= overload:
        raise AssertionError("Missing reference function: " + name)
    match = matches[overload]
    start = match.start()
    brace = text.index("{", match.end())
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def union(text, name):
    start = text.index("union " + name)
    return text[start:text.index("\n};", start) + 3]


manifest = json.loads((reference / "provenance.json").read_text())
for name, digest in manifest["sha256"].items():
    assert hashlib.sha256((reference / name).read_bytes()).hexdigest() == digest, name

midea = (reference / "src/ir_Midea.cpp").read_text()
midea_header = (reference / "src/ir_Midea.h").read_text()
coolix = (reference / "src/ir_Coolix.cpp").read_text()
coolix_header = (reference / "src/ir_Coolix.h").read_text()
bosch = (reference / "src/ir_Bosch.cpp").read_text()
bosch_header = (reference / "src/ir_Bosch.h").read_text()
sender = (reference / "src/IRsend.cpp").read_text()
sender_header = (reference / "src/IRsend.h").read_text()
protocol_constants = (reference / "src/IRremoteESP8266.h").read_text()
utils = (reference / "src/IRutils.cpp").read_text()

midea_methods = ["stateReset", "getRaw", "setPower", "setUseCelsius", "setTemp",
                 "getTemp", "setFan", "setMode", "calcChecksum", "checksum"]
coolix_methods = ["stateReset", "getRaw", "setRaw", "isSpecialState",
                  "handleSpecialState", "updateAndSaveState", "recoverSavedState",
                  "setTempRaw", "getTempRaw", "setTempFRange", "clearTempFRange",
                  "setTemp", "getTemp", "setSensorTempRaw", "setPower",
                  "setZoneFollow", "clearSensorTemp", "setMode", "getMode",
                  "getFan", "setFan", "setSwing", "setSwingVStep", "getSwingVStep",
                  "setSleep", "setTurbo", "setLed", "setClean", "send"]
bosch_methods = ["stateReset", "getRaw", "setRaw", "setPower", "setTempRaw",
                 "setTemp", "setUseFahrenheit", "setFan", "setMode",
                 "setInvertBytes", "setCheckSumS3"]

parts = ["""// Generated from pinned LGPL-2.1 upstream sources, not production.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <vector>
namespace upstream {
struct Segment { bool mark; uint32_t duration; };
class IRsend {
 public:
  uint32_t carrier = 0;
  uint8_t duty = 0;
  std::vector<Segment> segments;
  void enableIROut(uint32_t frequency, uint8_t dutycycle = 50) {
    carrier = frequency < 1000 ? frequency * 1000 : frequency;
    duty = dutycycle;
  }
  void append(bool level, uint32_t duration) {
    if (!duration) return;
    if (!segments.empty() && segments.back().mark == level)
      segments.back().duration += duration;
    else segments.push_back({level, duration});
  }
  void mark(uint32_t us) { append(true, us); }
  void space(uint32_t us) { append(false, us); }
  void sendData(uint16_t, uint32_t, uint16_t, uint32_t, uint64_t, uint16_t, bool);
  void sendMidea(uint64_t, uint16_t, uint16_t);
  void sendCOOLIX(uint64_t, uint16_t, uint16_t);
  void sendCoolix48(uint64_t, uint16_t, uint16_t) {
    // All public settings/actions are Celsius. Any accidental Fahrenheit-only
    // branch in the actual upstream send method must fail this test clearly.
    assert(false && "Unexpected Fahrenheit Coolix48 branch");
  }
  void sendBosch144(const unsigned char[], uint16_t, uint16_t);
  void sendGeneric(uint16_t, uint32_t, uint16_t, uint32_t, uint16_t,
                   uint32_t, uint16_t, uint32_t, const uint8_t*,
                   uint16_t, uint16_t, bool, uint16_t, uint8_t);
};
"""]
for name, text in (("kDefaultMessageGap", sender_header), ("kDutyDefault", sender_header),
                   ("kBosch144StateLength", protocol_constants),
                   ("kNoRepeat", protocol_constants), ("kSingleRepeat", protocol_constants),
                   ("kCoolixBits", protocol_constants), ("kCoolix48Bits", protocol_constants),
                   ("kCoolixDefaultRepeat", protocol_constants)):
    parts.append(re.search(r"^const [^\n]*\b" + name + r"\b[^\n]*", text, re.M).group())
for source in (midea, coolix):
    parts.append(source[source.index("// Constants"):source.index("using irutils::")])
parts.append(union(midea_header, "MideaProtocol"))
parts.append(midea_header[midea_header.index("// Constants"):midea_header.index("// Legacy defines")])
parts.append(coolix_header[coolix_header.index("// Constants"):coolix_header.index("// Classes")])
bosch_constants = bosch_header[bosch_header.index("// Constants"):bosch_header.index("// Classes")]
parts.append(re.sub(r"^using irutils::[^\n]*\n", "", bosch_constants, flags=re.M))
for name in ("reverseBits", "celsiusToFahrenheit", "fahrenheitToCelsius"):
    parts.append(function(utils, name))
parts.append("uint8_t sumBytes(const uint8_t*, uint16_t, uint8_t = 0);")
parts.append(function(utils, "sumBytes"))
parts.append("""
class IRMideaAC {
 public:
  MideaProtocol _{};
  bool _CleanToggle{}, _EconoToggle{}, _8CHeatToggle{}, _LightToggle{};
  bool _Quiet{}, _Quiet_prev{}, _SwingVToggle{}, _TurboToggle{};
  IRMideaAC() { stateReset(); }
  void stateReset();
  uint64_t getRaw();
  void setPower(bool);
  void setUseCelsius(bool);
  void setTemp(uint8_t, bool = false);
  uint8_t getTemp(bool = false) const;
  void setFan(uint8_t);
  void setMode(uint8_t);
  static uint8_t calcChecksum(uint64_t);
  void checksum();
};
class IRCoolixAC {
 public:
  IRsend _irsend;
  CoolixProtocol _{}, _saved{};
  bool powerFlag{}, turboFlag{}, ledFlag{}, cleanFlag{}, sleepFlag{}, swingFlag{};
  bool tempLowF{}, tempHighF{};
  uint8_t savedFan{};
  IRCoolixAC() { stateReset(); }
  void stateReset();
  uint32_t getRaw() const;
  void setRaw(uint32_t);
  bool isSpecialState() const;
  bool handleSpecialState(uint32_t);
  void updateAndSaveState(uint32_t);
  void recoverSavedState();
  void setTempRaw(uint8_t);
  uint8_t getTempRaw() const;
  void setTempFRange(bool);
  void clearTempFRange();
  void setTemp(uint8_t);
  uint8_t getTemp() const;
  void setSensorTempRaw(uint8_t);
  void setPower(bool);
  void setZoneFollow(bool);
  void clearSensorTemp();
  void setMode(uint8_t);
  uint8_t getMode() const;
  uint8_t getFan() const;
  void setFan(uint8_t, bool = true);
  void setSwing();
  void setSwingVStep();
  bool getSwingVStep() const;
  void setSleep();
  void setTurbo();
  void setLed();
  void setClean();
  void send(uint16_t = kCoolixDefaultRepeat);
};
class IRBosch144AC {
 public:
  Bosch144Protocol _{};
  bool powerFlag{};
  IRBosch144AC() { stateReset(); }
  void stateReset();
  unsigned char* getRaw();
  void setRaw(const uint8_t[], uint16_t = kBosch144StateLength);
  void setPower(bool);
  void setTempRaw(uint8_t);
  void setTemp(uint8_t, bool = false);
  void setUseFahrenheit(bool);
  void setFan(uint16_t);
  void setMode(uint8_t);
  void setInvertBytes();
  void setCheckSumS3();
};
""")
parts.extend(function(midea, "IRMideaAC::" + name) for name in midea_methods)
for name in coolix_methods:
    if name == "send":
        # The pinned upstream repeat_override expression intentionally relies
        # on '-' binding before '?:'. Keep its bytes/semantics unchanged and
        # scope this known third-party diagnostic to that single function.
        parts.append('#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wparentheses"')
    parts.append(function(coolix, "IRCoolixAC::" + name))
    if name == "send":
        parts.append('#pragma GCC diagnostic pop')
parts.extend(function(bosch, "IRBosch144AC::" + name) for name in bosch_methods)
parts.append(function(sender, "IRsend::sendData"))
parts.append(function(midea, "IRsend::sendMidea"))
parts.append(function(coolix, "IRsend::sendCOOLIX"))
parts.append(function(sender, "IRsend::sendGeneric", 2))  # Byte-array overload, verbatim.
parts.append(function(bosch, "IRsend::sendBosch144"))
parts.append("}  // namespace upstream\n")

compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
if not compiler:
    raise SystemExit("A C++17 compiler is required")
source_sha = hashlib.sha256(production.read_bytes()).hexdigest()
with tempfile.TemporaryDirectory(prefix="cardputer-ac-protocol-test-") as temporary:
    root = Path(temporary)
    (root / "upstream_reference.h").write_text("\n".join(parts))
    binary = root / "test_ac_protocol"
    subprocess.run([
        compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(factory / "main"), "-I", str(root),
        str(factory / "tests/test_ac_protocol.cpp"), "-o", str(binary),
    ], check=True)
    environment = os.environ.copy()
    environment["ASAN_OPTIONS"] = "halt_on_error=1:detect_leaks=" + ("0" if sys.platform == "darwin" else "1")
    environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    subprocess.run([str(binary)], check=True, env=environment, timeout=30)
    assert hashlib.sha256(production.read_bytes()).hexdigest() == source_sha
    print("Compared 59 actual upstream methods/helpers, native bitfields, constants and temperature maps")
    print("Upstream commit:", manifest["commit"])
    print("Production ac_protocol.h SHA-256:", source_sha)
