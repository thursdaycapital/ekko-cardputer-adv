#!/usr/bin/env python3
"""Compile actual keyboard and TCA8418 implementations with a register-level I2C fake."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

factory = Path(__file__).resolve().parent.parent
keyboard = factory / "main/hal/keyboard/keyboard.cpp"
tca = factory / "main/hal/utils/adafruit_tca8418/Adafruit_TCA8418.cpp"
sources = [keyboard, keyboard.with_suffix(".h"), tca, tca.with_suffix(".h")]
hashes = {source: hashlib.sha256(source.read_bytes()).hexdigest() for source in sources}
compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
if not compiler:
    raise SystemExit("A C++17 compiler is required")
with tempfile.TemporaryDirectory(prefix="cardputer-keyboard-") as temporary:
    binary = Path(temporary) / "test_keyboard"
    subprocess.run([
        compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(factory / "tests/keyboard_host"),
        "-I", str(factory / "components/mooncake_log/src"),
        "-I", str(factory / "main"), str(keyboard), str(tca),
        str(factory / "tests/test_keyboard.cpp"), "-o", str(binary),
    ], check=True)
    environment = os.environ.copy()
    environment["ASAN_OPTIONS"] = "halt_on_error=1:detect_leaks=" + ("0" if sys.platform == "darwin" else "1")
    environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    subprocess.run([str(binary)], check=True, env=environment, timeout=20)
for source, digest in hashes.items():
    if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
        raise SystemExit(f"Production {source.name} changed during tests; rerun")
    print(f"Actual {source.name} SHA-256: {digest}")
print("I2C/GPIO are test doubles; real hardware key operation needs device confirmation.")
