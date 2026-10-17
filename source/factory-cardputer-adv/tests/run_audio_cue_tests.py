#!/usr/bin/env python3
"""Compile actual audio.cpp against an asynchronous SDK boundary fake."""
from pathlib import Path
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

factory = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--negative-control", type=Path, help="Original audio.cpp; it must fail ASan after returning")
args = parser.parse_args()
production = factory / "main/apps/utils/audio/audio.cpp"
source_sha = hashlib.sha256(production.read_bytes()).hexdigest()
compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
if not compiler:
    raise SystemExit("A C++17 compiler is required")
environment = os.environ.copy()
environment["ASAN_OPTIONS"] = "halt_on_error=1:detect_leaks=" + ("0" if sys.platform == "darwin" else "1")
environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
with tempfile.TemporaryDirectory(prefix="factory-audio-cue-test-") as temporary:
    def compile_audio(source, binary):
        subprocess.run([
            compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(factory / "tests/audio_cue_host"), "-I", str(factory / "main"),
            str(factory / "tests/test_audio_cues.cpp"), str(source), "-o", str(binary),
        ], check=True)
    binary = Path(temporary) / "test_audio_cues"
    compile_audio(production, binary)
    subprocess.run([str(binary)], check=True, env=environment, timeout=20)
    if args.negative_control:
        baseline = Path(temporary) / "old_audio_cues"
        compile_audio(args.negative_control.resolve(), baseline)
        result = subprocess.run([str(baseline)], env=environment, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True, timeout=20)
        if result.returncode == 0 or "heap-use-after-free" not in result.stderr:
            raise SystemExit("Negative control did not expose the original asynchronous PCM lifetime bug")
        print("Negative control: original audio.cpp fails ASan heap-use-after-free after play_tone returned")
    if hashlib.sha256(production.read_bytes()).hexdigest() != source_sha:
        raise SystemExit("Production audio.cpp changed during test; run again")
    print("Production audio.cpp SHA-256:", source_sha)
