#!/usr/bin/env python3
"""Extract and compile actual SDK methods with hardware/FreeRTOS test doubles."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

factory = Path(__file__).resolve().parent.parent
utility = factory / "components/M5Unified/src/utility"
compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
if not compiler:
    raise SystemExit("A C++17 compiler is required")


def method(source: str, signature: str) -> str:
    """Return the exact method text, handling C++ strings and comments."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    quote = None
    comment = None
    index = opening
    while index < len(source):
        char = source[index]
        following = source[index:index + 2]
        if comment == "line":
            if char == "\n":
                comment = None
        elif comment == "block":
            if following == "*/":
                comment = None
                index += 1
        elif quote:
            if char == "\\":
                index += 1
            elif char == quote:
                quote = None
        elif following == "//":
            comment = "line"
            index += 1
        elif following == "/*":
            comment = "block"
            index += 1
        elif char in ('"', "'"):
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
        index += 1
    raise ValueError(f"Cannot find the end of {signature}")


selected = {
    "Mic_Class.cpp": [
        "uint32_t Mic_Class::_calc_rec_rate(void) const",
        "void Mic_Class::mic_task(void* args)",
        "bool Mic_Class::begin(void)",
        "void Mic_Class::end(void)",
        "bool Mic_Class::_rec_raw(",
    ],
    "Speaker_Class.cpp": [
        "bool Speaker_Class::begin(void)",
        "void Speaker_Class::end(void)",
        "void Speaker_Class::stop(void)",
        "void Speaker_Class::wav_info_t::clear(void)",
        "void Speaker_Class::wav_info_t::assign(",
        "bool Speaker_Class::_set_next_wav(",
        "bool Speaker_Class::_play_raw(",
    ],
}
inline_selected = {
    "Mic_Class.hpp": ("Mic_Class", [
        "size_t isRecording(void) const",
        "size_t recordingQueueSize(void) const",
        "bool canRecord(void) const",
    ]),
    "Speaker_Class.hpp": ("Speaker_Class", [
        "size_t isPlaying(uint8_t channel) const volatile",
        "bool canPlay(uint8_t channel) const volatile",
    ]),
}
sources = {name: (utility / name).read_text() for name in [*selected, *inline_selected]}
digests = {name: hashlib.sha256(text.encode()).hexdigest() for name, text in sources.items()}
extracted = "\n\n".join(method(sources[name], signature) for name, signatures in selected.items() for signature in signatures)
for name, (class_name, signatures) in inline_selected.items():
    for signature in signatures:
        original = method(sources[name], signature)
        return_type, rest = original.split(" ", 1)
        extracted += "\n\n" + return_type + " " + class_name + "::" + rest

with tempfile.TemporaryDirectory(prefix="cardputer-record-sdk-test-") as temporary:
    directory = Path(temporary)
    unit = directory / "actual_sdk_methods.cpp"
    unit.write_text('#include "record_sdk_host.h"\nnamespace m5 {\n' + extracted + '\n}\n')
    binary = directory / "test_record_sdk"
    subprocess.run([
        compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra",
        "-Wno-unused-variable", "-Wno-unused-but-set-variable", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(factory / "tests"), str(factory / "tests/test_record_sdk.cpp"),
        str(unit), "-o", str(binary),
    ], check=True)
    environment = os.environ.copy()
    environment["ASAN_OPTIONS"] = "halt_on_error=1:detect_leaks=" + ("0" if sys.platform == "darwin" else "1")
    environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    subprocess.run([str(binary)], check=True, env=environment, timeout=20)
    for name, digest in digests.items():
        if hashlib.sha256((utility / name).read_bytes()).hexdigest() != digest:
            raise SystemExit(f"{name} changed during testing; run again")
        print(f"Actual {name} SHA-256: {digest}")
    total_methods = sum(map(len, selected.values())) + sum(len(value[1]) for value in inline_selected.values())
    print("Extracted methods:", len(extracted.splitlines()), "lines;", total_methods, "methods (including actual inline queue APIs)")
