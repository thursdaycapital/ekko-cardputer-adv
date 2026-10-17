"""Check actual selection state and canonical catalog, without hardware."""
import json
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
repository = firmware.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--catalog", type=Path, default=firmware / "config/agents.json" if (firmware / "config/agents.json").is_file() else repository / "config/agents.json")
arguments = parser.parse_args()
catalog = json.loads(arguments.catalog.read_text())["agents"]
header = (firmware / "include/AgentSelection.h").read_text()
embedded = re.findall(r'\{"([^"\n]+)",\s*"([^"\n]+)",\s*"[^"\n]+"\}', header)
canonical = [(a["id"], a["name"]) for a in catalog]
if embedded != canonical or len(embedded) != 16:
    raise SystemExit("Embedded agent IDs/order/names drifted from config/agents.json")
compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise SystemExit("C++17 compiler required")
with tempfile.TemporaryDirectory(prefix="cardputer-native-tests-") as temp:
    binary = Path(temp) / "agent-selection"
    subprocess.run([
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"),
        str(firmware / "tests/test_agent_selection.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
    navigation = Path(temp) / "navigation-input"
    subprocess.run([
        compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"),
        str(firmware / "tests/test_navigation_input.cpp"), "-o", str(navigation),
    ], check=True)
    subprocess.run([str(navigation)], check=True)
    remote_fallback = Path(temp) / "remote-fallback"
    subprocess.run([
        compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"),
        str(firmware / "tests/test_remote_fallback.cpp"), "-o", str(remote_fallback),
    ], check=True)
    subprocess.run([str(remote_fallback)], check=True)
    mcu_reauth = Path(temp) / "mcu-reauth"
    subprocess.run([
        compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"),
        str(firmware / "tests/test_mcu_reauth.cpp"), "-o", str(mcu_reauth),
    ], check=True)
    subprocess.run([str(mcu_reauth)], check=True)
    conversation = Path(temp) / "conversation-view"
    subprocess.run([
        compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"),
        str(firmware / "tests/test_conversation_view.cpp"), "-o", str(conversation),
    ], check=True)
    subprocess.run([str(conversation)], check=True)
    keyboard = Path(temp) / "chat-keyboard"
    subprocess.run([
        compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"),
        str(firmware / "tests/test_chat_keyboard.cpp"), "-o", str(keyboard),
    ], check=True)
    subprocess.run([str(keyboard)], check=True)
    key_click = Path(temp) / "key-click"
    subprocess.run([
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"),
        str(firmware / "tests/test_key_click.cpp"), "-o", str(key_click),
    ], check=True)
    subprocess.run([str(key_click)], check=True)
    envelope = Path(temp) / "native-envelope"
    json_headers = firmware / ".pio/libdeps/cardputer-adv/ArduinoJson/src"
    if not json_headers.is_dir():
        raise SystemExit("Run the pinned PlatformIO build to install ArduinoJson first")
    subprocess.run([
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"), "-I", str(json_headers),
        str(firmware / "tests/test_native_envelope.cpp"), "-o", str(envelope),
    ], check=True)
    subprocess.run([str(envelope)], check=True)
print("Canonical catalog: all 16 IDs, names and order match")
