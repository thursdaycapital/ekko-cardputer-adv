#!/usr/bin/env python3
"""Exercise the production SD WAV module against a real temporary filesystem."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import wave

factory = Path(__file__).resolve().parent.parent
production = factory / "main/apps/app_record/record_storage.h"
source_sha = hashlib.sha256(production.read_bytes()).hexdigest()
compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
if not compiler:
    raise SystemExit("A C++17 compiler is required")

with tempfile.TemporaryDirectory(prefix="cardputer-sd-wav-test-") as temporary:
    root = Path(temporary)
    binary = root / "test_record_storage"
    files = root / "card"
    files.mkdir()
    subprocess.run([
        compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(factory / "main"),
        str(factory / "tests/test_record_storage.cpp"), "-o", str(binary),
    ], check=True)
    environment = os.environ.copy()
    environment["ASAN_OPTIONS"] = "halt_on_error=1:detect_leaks=" + ("0" if sys.platform == "darwin" else "1")
    environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    subprocess.run([str(binary), str(files)], check=True, env=environment, timeout=30)
    with wave.open(str(files / "RECORD/REC00002.WAV"), "rb") as recording:
        assert recording.getnchannels() == 1
        assert recording.getsampwidth() == 2
        assert recording.getframerate() == 16000
        assert recording.getnframes() == 40001
        assert len(recording.readframes(40002)) == 80002
    with wave.open(str(files / "FULL/REC00001.WAV"), "rb") as partial:
        assert partial.getnframes() == 1 and len(partial.readframes(2)) == 2
    if hashlib.sha256(production.read_bytes()).hexdigest() != source_sha:
        raise SystemExit("Production header changed while tests ran; run again")
    print("Python wave independently validated normal and short-write recovery WAV files")
    print("Production record_storage.h SHA-256:", source_sha)
