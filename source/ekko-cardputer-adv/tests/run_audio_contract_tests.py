"""Compare production C++ decode functions with production TS ADPCM, offline."""
import base64
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
repository = firmware.parents[2]
compiler = shutil.which("clang++") or shutil.which("g++")
node = shutil.which("node")
if not compiler or not node:
    raise SystemExit("C++11 compiler and Node with TypeScript type stripping required")
source = (firmware / "src/main.cpp").read_text()
parts = []
for name in ("kImaAdpcmIndexTable", "kImaAdpcmStepTable"):
    match = re.search(r"const int " + name + r"\[\d+\]\s*=\s*\{[^}]+\};", source)
    if not match:
        raise SystemExit(f"Cannot locate production array {name}")
    parts.append(match.group())
for signature in ("uint16_t readLe16(", "uint32_t readLe32(", "int16_t decodeImaAdpcmNibble("):
    start = source.find(signature)
    if start < 0:
        raise SystemExit(f"Cannot locate production function {signature}")
    brace = source.index("{", start)
    nesting, end = 1, brace + 1
    while nesting and end < len(source):
        if source[end] == "{":
            nesting += 1
        elif source[end] == "}":
            nesting -= 1
        end += 1
    if nesting:
        raise SystemExit("Unbalanced production function")
    parts.append(source[start:end])
production = "\n\n".join(parts)
javascript = r"""
import { pathToFileURL } from 'node:url';
const {encodeMcuImaAdpcm, decodeMcuImaAdpcm} = await import(pathToFileURL(process.argv[1]).href);
const cases = [];
function add(name, samples) {
  const pcm = Buffer.alloc(samples.length * 2);
  samples.forEach((sample, i) => pcm.writeInt16LE(sample, i * 2));
  const encoded = encodeMcuImaAdpcm(pcm, 24000);
  const decoded = decodeMcuImaAdpcm(encoded);
  if (decoded.channels !== 1 || decoded.sampleRate !== 24000) throw new Error('wrong format');
  cases.push({name, encoded: encoded.toString('base64'), decoded: decoded.pcm.toString('base64')});
}
for (const length of [0, 1, 2, 3, 255, 256, 257, 511, 512, 513, 1024, 1025])
  add(`silence-${length}`, Array(length).fill(0));
add('sine-440', Array.from({length:4097}, (_, i) => Math.round(12000 * Math.sin(i * 2 * Math.PI * 440 / 24000))));
add('positive-dc', Array(1025).fill(10000));
add('negative-dc', Array(1025).fill(-10000));
add('alternating-extremes', Array.from({length:2049}, (_, i) => i % 2 ? 32767 : -32768));
add('ramp', Array.from({length:4097}, (_, i) => (i * 71 % 65536) - 32768));
let random = 0x12345678;
add('deterministic-noise', Array.from({length:8193}, () => {random = (Math.imul(random,1664525) + 1013904223) >>> 0; return (random >>> 16) - 32768;}));
const valid = Buffer.from(cases.find(item => item.name === 'sine-440').encoded, 'base64');
let rejected = 0;
for (const mutate of [
  b => b.subarray(0,19), b => b.subarray(0,b.length-1),
  b => {b[0]=0;return b}, b => {b[4]=2;return b}, b => {b[5]=2;return b}, b => {b[18]=89;return b}
]) {
  let failed = false;
  try {decodeMcuImaAdpcm(mutate(Buffer.from(valid)))} catch {failed = true}
  if (!failed) throw new Error('invalid packet was accepted');
  rejected++;
}
process.stdout.write(JSON.stringify({cases,rejected}));
"""
encoded_result = subprocess.run([
    node, "--experimental-strip-types", "--input-type=module", "-e", javascript,
    str(repository / "packages/server/src/modules/studio/services/voice/mcu/adpcm.ts"),
], check=True, capture_output=True, text=True)
fixtures = json.loads(encoded_result.stdout)
with tempfile.TemporaryDirectory(prefix="cardputer-audio-contract-") as directory:
    temp = Path(directory)
    (temp / "firmware_adpcm_under_test.h").write_text("#pragma once\n#include <stdint.h>\n" + production)
    binary = temp / "native-adpcm-contract"
    subprocess.run([
        compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(temp), str(firmware / "tests/test_native_adpcm_contract.cpp"), "-o", str(binary),
    ], check=True)
    arguments = [str(binary)]
    for number, item in enumerate(fixtures["cases"]):
        encoded, decoded = temp / f"{number}.adpcm", temp / f"{number}.pcm"
        encoded.write_bytes(base64.b64decode(item["encoded"]))
        decoded.write_bytes(base64.b64decode(item["decoded"]))
        arguments.extend((str(encoded), str(decoded)))
    subprocess.run(arguments, check=True)
print(f"Actual server decoder rejected {fixtures['rejected']} malformed packets; C++ source SHA256 {hashlib.sha256(production.encode()).hexdigest()}")
