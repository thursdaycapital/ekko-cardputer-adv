#!/usr/bin/env python3
"""Compile the actual native RMT transport as C, with asynchronous driver fakes."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

factory = Path(__file__).resolve().parent.parent
source = factory / "main/hal/utils/ir_nec/ir_helper.c"
digest = hashlib.sha256(source.read_bytes()).hexdigest()
compiler = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc")
if not compiler:
    raise SystemExit("A C11 compiler is required")
with tempfile.TemporaryDirectory(prefix="cardputer-ir-rmt-") as temporary:
    binary = Path(temporary) / "ir_rmt_tests"
    subprocess.run([
        compiler, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(factory / "tests/ir_rmt_host"),
        "-I", str(source.parent), str(source),
        str(factory / "tests/ir_rmt_transport.c"), "-o", str(binary),
    ], check=True)
    environment = os.environ.copy()
    environment["ASAN_OPTIONS"] = "halt_on_error=1:detect_stack_use_after_return=1:detect_leaks=" + ("0" if sys.platform == "darwin" else "1")
    environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    subprocess.run([str(binary)], check=True, env=environment, timeout=20)
if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
    raise SystemExit("Production transport changed during tests; rerun")
print("Actual ir_helper.c SHA-256:", digest)
print("Hardware RMT timing and IR receiver response require device validation.")
