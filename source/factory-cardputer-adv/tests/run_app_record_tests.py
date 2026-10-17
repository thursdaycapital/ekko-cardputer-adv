#!/usr/bin/env python3
"""Compile production AppRecord with an asynchronous fake HAL and sanitizers."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

factory = Path(__file__).resolve().parent.parent
production = factory / "main/apps/app_record/app_record.cpp"
sources = [production, production.with_suffix(".h"), production.with_name("record_storage.h")]
source_hashes = {source: hashlib.sha256(source.read_bytes()).hexdigest() for source in sources}
compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
if not compiler:
    raise SystemExit("A C++17 compiler is required")

with tempfile.TemporaryDirectory(prefix="cardputer-record-test-") as temporary:
    binary = Path(temporary) / "test_app_record"
    subprocess.run([
        compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(factory / "tests/record_host"),
        "-I", str(factory / "main"),
        str(factory / "tests/test_app_record.cpp"),
        str(production),
        "-o", str(binary),
    ], check=True)
    environment = os.environ.copy()
    # Apple's AddressSanitizer does not provide LeakSanitizer. Keep ASan and
    # UBSan enabled on macOS; add leak detection where the runtime supports it.
    environment["ASAN_OPTIONS"] = "halt_on_error=1:detect_leaks=" + ("0" if sys.platform == "darwin" else "1")
    environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    subprocess.run([str(binary)], check=True, env=environment, timeout=20)
    for source, expected in source_hashes.items():
        if hashlib.sha256(source.read_bytes()).hexdigest() != expected:
            raise SystemExit(f"Production source {source.name} changed while tests ran; run again")
        print(f"Production {source.name} SHA-256:", expected)
