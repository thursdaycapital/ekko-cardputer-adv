#!/usr/bin/env python3
"""Run the actual AppRemote UI with an asynchronous HAL fake and sanitizers."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

factory = Path(__file__).resolve().parent.parent
production = factory / "main/apps/app_remote/app_remote.cpp"
sources = [production, production.with_suffix(".h"), production.with_name("ac_protocol.h")]
hashes = {source: hashlib.sha256(source.read_bytes()).hexdigest() for source in sources}
compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
if not compiler:
    raise SystemExit("A C++17 compiler is required")
with tempfile.TemporaryDirectory(prefix="cardputer-remote-ui-test-") as temporary:
    binary = Path(temporary) / "test_app_remote"
    subprocess.run([
        compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(factory / "tests/remote_host"), "-I", str(factory / "main"),
        str(factory / "tests/test_app_remote.cpp"), str(production), "-o", str(binary),
    ], check=True)
    environment = os.environ.copy()
    environment["ASAN_OPTIONS"] = "halt_on_error=1:detect_leaks=" + ("0" if sys.platform == "darwin" else "1")
    environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    subprocess.run([str(binary)], check=True, env=environment, timeout=20)
    for source, digest in hashes.items():
        if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
            raise SystemExit(f"Production {source.name} changed during tests; run again")
        print(f"Production {source.name} SHA-256: {digest}")
