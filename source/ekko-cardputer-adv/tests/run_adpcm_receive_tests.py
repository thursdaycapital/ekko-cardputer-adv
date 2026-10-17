"""Run actual firmware ADPCM receive/playback code against host IO/task boundaries.

This extracts the production class/functions without rewriting their algorithms.
The C++ harness supplies bounded WiFi reads, clock, heap and I2S boundaries.
It does not model ESP32 TLS internals, DMA or audible timing.
"""
from pathlib import Path
import hashlib
import re
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise SystemExit("C++11 compiler with pthread and ASan/UBSan required")
source = (firmware / "src/main.cpp").read_text()


def definition(signature, trailing_semicolon=False):
    start = source.find(signature)
    if start < 0:
        raise SystemExit(f"Production definition not found: {signature}")
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth and end < len(source):
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    if depth:
        raise SystemExit(f"Unbalanced production definition: {signature}")
    return source[start:end] + (";" if trailing_semicolon else "")


pieces = []
for name in (
    "kMcuAudioHttpTimeoutMs", "kMcuAdpcmHeaderBytes", "kMcuAdpcmReadChunkBytes",
    "kMcuAdpcmOutputFrames", "kMcuAdpcmReceiveBytes", "kMcuAdpcmPrebufferMs",
    "kMcuAudioDefaultSampleRate",
):
    match = re.search(r"constexpr\s+\w+\s+" + name + r"\s*=\s*[^;]+;", source)
    if not match:
        raise SystemExit(f"Production constant not found: {name}")
    pieces.append(match.group())
pieces.extend([
    definition("struct AdpcmReceiveReport {", True),
    "AdpcmReceiveReport lastAdpcmReceive;",
    "uint8_t mcuAdpcmInputBuffer[kMcuAdpcmReadChunkBytes];",
    "int16_t mcuAdpcmStereoBuffer[kMcuAdpcmOutputFrames * 2];",
])
for name in ("kImaAdpcmIndexTable", "kImaAdpcmStepTable"):
    match = re.search(r"const int " + name + r"\[\d+\]\s*=\s*\{[^}]+\};", source)
    if not match:
        raise SystemExit(f"Production ADPCM table not found: {name}")
    pieces.append(match.group())
for signature in (
    "uint16_t readLe16(", "uint32_t readLe32(", "int16_t decodeImaAdpcmNibble(",
    "bool flushAdpcmStereo(", "bool pushAdpcmSample(", "bool readExactAudioBytes(",
):
    pieces.append(definition(signature))
pieces.append(definition("class AdpcmDownload {", True))
pieces.append(definition("bool playAdpcmStream("))
production = "\n\n".join(pieces)
with tempfile.TemporaryDirectory(prefix="cardputer-adpcm-receive-") as directory:
    temp = Path(directory)
    (temp / "firmware_receive_under_test.h").write_text(production)
    binary = temp / "adpcm-receive"
    subprocess.run([
        compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pthread",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(temp), "-I", str(firmware / "include"),
        str(firmware / "tests/test_adpcm_receive.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True, timeout=60)
print(f"Extracted production receive/playback SHA256 {hashlib.sha256(production.encode()).hexdigest()}")
