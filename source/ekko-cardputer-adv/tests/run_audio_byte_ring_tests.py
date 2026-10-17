"""Exercise the real SPSC byte ring with ASan/UBSan and native threads."""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise SystemExit("C++11 compiler required")
with tempfile.TemporaryDirectory(prefix="cardputer-audio-byte-ring-") as temp:
    binary = Path(temp) / "audio-byte-ring"
    subprocess.run([
        compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pthread",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"),
        str(firmware / "tests/test_audio_byte_ring.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True, timeout=60)
