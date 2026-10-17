"""Run genuine vendor font raster/measurement and POSIX storage tests on host.

Only the LCD drawing/color sink is replaced. Every font/UTF-8/layout method is
read verbatim from this checkout's M5GFX sources at test time; no font decoder or
width algorithm is reimplemented. This does not exercise ESP-IDF or the LCD.
"""
from pathlib import Path
import argparse
import ast
import hashlib
import json
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--font-assets", type=Path, default=root / "fonts/fusion-pixel",
                    help="Font asset root containing EkkoPixel12Compatible.bdf and upstream/fusion-pixel-12px-monospaced-zh_hans.bdf")
parser.add_argument("--report", type=Path, default=root / "tests/font-storage-validation.json")
args = parser.parse_args()
compiler = shutil.which("clang++") or shutil.which("g++")
assert compiler, "A host C++17 compiler is required"
vendor = root / "components/M5GFX/src/lgfx/v1"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def bdf_records(path):
    result = {}
    record = None
    bitmap = False
    for line in path.read_text().splitlines():
        if line.startswith("STARTCHAR "):
            record = {"bitmap": []}
            bitmap = False
        elif line.startswith("ENCODING "):
            record["codepoint"] = int(line.split()[1])
        elif line.startswith("DWIDTH "):
            record["advance"] = int(line.split()[1])
        elif line.startswith("BBX "):
            record["bbx"] = [int(value) for value in line.split()[1:]]
        elif line == "BITMAP":
            bitmap = True
        elif line == "ENDCHAR":
            assert len(record["bitmap"]) == record["bbx"][1]
            if record["codepoint"] >= 0:
                result[record["codepoint"]] = record
            record = None
            bitmap = False
        elif bitmap:
            record["bitmap"].append(line)
    return result


combined_path = args.font_assets / "EkkoPixel12Compatible.bdf"
fusion_path = args.font_assets / "upstream/fusion-pixel-12px-monospaced-zh_hans.bdf"
combined = bdf_records(combined_path)
fusion = bdf_records(fusion_path)
fusion_matches = 0
for cp, glyph in combined.items():
    if cp in fusion:
        assert glyph == fusion[cp], f"Original Fusion BDF differs at U+{cp:04X}"
        fusion_matches += 1

# Extract actual C++ string literals, excluding comments and binary/third-party
# assets. The union is deliberately broader than visible paths: it catches all
# fixed Chinese messages, including error/status branches and format literals.
token = re.compile(r'''//[^\n]*|/\*.*?\*/|'(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*"''', re.S)
labels = []
ui_source_hashes = {}
for source in sorted((root / "main").rglob("*")):
    if source.suffix not in (".cpp", ".h"):
        continue
    if set(source.parts) & {"assets", "PikaScript", "rs_toolkit"} or source.name == "EkkoPixelFont12.cpp":
        continue
    content = source.read_text()
    previous_count = len(labels)
    for match in token.finditer(content):
        if not match.group().startswith('"'):
            continue
        value = ast.literal_eval(match.group())
        if not any(ord(char) >= 0x80 for char in value):
            continue
        line = content.count("\n", 0, match.start()) + 1
        for logical_line in value.splitlines():
            if logical_line:
                labels.append({"file": str(source.relative_to(root)), "line": line, "text": logical_line})
    if len(labels) != previous_count:
        ui_source_hashes[str(source.relative_to(root))] = sha(content.encode())
characters = {char for label in labels for char in label["text"] if ord(char) >= 0x80}
missing = sorted(char for char in characters if ord(char) not in combined)
assert not missing, "Original BDF missing fixed UI characters: " + repr(missing)


def exact_slice(text, begin, end):
    return text[text.index(begin):text.index(end, text.index(begin))]


fonts_source = (vendor / "lgfx_fonts.cpp").read_text()
base_source = (vendor / "LGFXBase.cpp").read_text()
methods = [
    exact_slice(fonts_source, "  size_t IFont::drawCharDummy(", "  void BaseFont::getDefaultMetric("),
    exact_slice(fonts_source, "  struct u8g2_font_decode_t", "  void VLWfont::getDefaultMetric("),
    exact_slice(base_source, "  uint16_t LGFXBase::decodeUTF8(", "  int32_t LGFXBase::fontHeight("),
    exact_slice(base_source, "  int32_t LGFXBase::textWidth(", "  size_t LGFXBase::drawNumber("),
    exact_slice(base_source, "  size_t LGFXBase::draw_string(", "  size_t LGFXBase::write(uint8_t"),
]
gps_source = (root / "main/apps/app_gps/app_gps.cpp").read_text()
gps_time_format = next(ast.literal_eval(match.group()) for match in token.finditer(gps_source)
                       if match.group().startswith('"世界时 {}'))
gps_coordinate_format = next(ast.literal_eval(match.group()) for match in token.finditer(gps_source)
                             if match.group().startswith('"{} {:.6f}'))
gps_speed_format = next(ast.literal_eval(match.group()) for match in token.finditer(gps_source)
                        if match.group().startswith('"速度:{:.1f}'))
efont_source = (vendor.parent / "Fonts/efont/lgfx_efont_cn.c").read_text()
efont_array = re.search(r'PROGMEM const uint8_t lgfx_efont_cn_12\[\d+\]\s*=\s*(?:"(?:\\.|[^"\\])*"\s*)+;', efont_source)
assert efont_array, "Actual factory efontCN_12 data not found"
report = {
    "hostOnly": True,
    "fullApplicationOrLcdTest": False,
    "limitations": [
        "Host memory drawing sink; the physical LCD, ESP-IDF target and complete application screens are not executed here.",
        "No IMU, SD card hardware, infrared, LoRa or GPS peripheral was exercised.",
        "The preview renders actual menu title glyphs, not launcher icons, animation or the complete launcher layout.",
        "Fixed UI literals and representative formatted GPS bounds are covered; arbitrary user data layout is not claimed.",
    ],
    "vendorFontSourceSha256": sha(fonts_source.encode()),
    "vendorLayoutSourceSha256": sha(base_source.encode()),
    "exactCompiledMethodSlicesSha256": sha("".join(methods).encode()),
    "combinedBdfSha256": sha(combined_path.read_bytes()),
    "originalFusionBdfSha256": sha(fusion_path.read_bytes()),
    "originalFusionGlyphsExact": fusion_matches,
    "fixedUiLiteralLines": len(labels),
    "fixedUiNonAsciiCharacters": len(characters),
    "fixedUiCoveredCharacters": "".join(sorted(characters)),
    "fixedUiCoveredCodepoints": [f"U+{ord(char):04X}" for char in sorted(characters)],
    "fixedUiOriginalEfontSupplementCharacters": "".join(sorted(char for char in characters if ord(char) not in fusion)),
    "fixedUiSourcesSha256": ui_source_hashes,
    "missingFixedUiCharacters": missing,
    "sanitizers": ["address", "undefined"],
    "callerClipModes": ["12px native line", "narrow parent intersection", "disjoint parent clip"],
}
flags = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
with tempfile.TemporaryDirectory(prefix="factory-font-storage-validation-") as temporary:
    temporary = Path(temporary)
    methods_file = temporary / "actual_vendor_methods.cpp"
    methods_file.write_text('#include "M5GFX.h"\nnamespace lgfx { inline namespace v1 {\n' + "".join(methods) + "\n} }\n")
    efont_file = temporary / "actual_factory_efont.cpp"
    efont_file.write_text("#include <cstdint>\nextern const uint8_t lgfx_efont_cn_12[];\n#define PROGMEM\n" + efont_array.group())
    format_fixtures = temporary / "actual_ui_formats.h"
    format_fixtures.write_text(
        '#include <fmt/format.h>\n'
        + 'inline std::string gpsTimeSample() { return fmt::format('
        + json.dumps(gps_time_format, ensure_ascii=False) + ', 2026, 12, 31, 23, 59, 59); }\n'
        + 'inline std::string gpsCoordinateSample() { return fmt::format('
        + json.dumps(gps_coordinate_format, ensure_ascii=False) + ', "西经", -180.0); }\n'
        + 'inline std::string gpsSpeedSample() { return fmt::format('
        + json.dumps(gps_speed_format, ensure_ascii=False) + ', 999.9); }\n')
    records = temporary / "original-bdf-records.txt"
    with records.open("w") as out:
        for cp, glyph in sorted(combined.items()):
            values = [cp, glyph["advance"], *glyph["bbx"], int(cp not in fusion)]
            out.write(" ".join(map(str, values)) + " " + " ".join(glyph["bitmap"]) + "\n")
    labels_file = temporary / "fixed-ui-labels.txt"
    labels_file.write_text("\n".join(label["text"] for label in labels) + "\n")
    binary = temporary / "pixel-font-runtime"
    subprocess.run(flags + ["-DFMT_HEADER_ONLY", "-I", str(temporary),
                            "-I", str(root / "components/mooncake_log/src"),
                            "-I", str(root / "tests/font_host"), "-I", str(vendor.parents[1]),
                            "-I", str(root / "main/ekko_shared"), str(methods_file), str(efont_file),
                            str(root / "main/ekko_shared/EkkoPixelFont12.cpp"),
                            str(root / "tests/test_pixel_font_runtime.cpp"), "-o", str(binary)], check=True)
    menu_image = temporary / "actual-menu-labels.pgm"
    runtime = subprocess.run([str(binary), str(records), str(labels_file), str(menu_image)], text=True, capture_output=True, check=True)
    report["menuWidths"] = {}
    report["formattedGpsSampleWidths"] = {}
    for line in runtime.stdout.splitlines():
        fields = line.split("\t")
        if fields[0] == "GLYPHS":
            report["glyphsRasterCompared"] = int(fields[1])
            report["pixelsCompared"] = int(fields[2])
            report["actualFactoryEfontGlyphsCompared"] = int(fields[3])
        elif fields[0] == "LABEL":
            labels[int(fields[1])]["actualWidth"] = int(fields[2])
        elif fields[0] == "MENU":
            report["menuWidths"][fields[1]] = int(fields[2])
        elif fields[0] == "FORMATTED":
            report["formattedGpsSampleWidths"][fields[1]] = float(fields[2])
    report["formatTemplatesWiderThan204"] = [label for label in labels if label["actualWidth"] > 204 and "{" in label["text"]]
    report["literalLinesWiderThan204"] = [label for label in labels if label["actualWidth"] > 204 and "{" not in label["text"]]
    report["fixedUiLiteralWidths"] = labels
    # Test artifacts stay within tests; compiler inputs and executables are temporary.
    converter = shutil.which("sips")
    preview = args.report.with_suffix(".png" if converter else ".pgm")
    preview.parent.mkdir(parents=True, exist_ok=True)
    if converter:
        subprocess.run([converter, "-s", "format", "png", str(menu_image), "--out", str(preview)],
                       text=True, capture_output=True, check=True)
    else:
        shutil.copyfile(menu_image, preview)
    report["actualRasterMenuTitlePreview"] = str(preview.relative_to(root)) if preview.is_relative_to(root) else str(preview)
    storage_binary = temporary / "sd-write-probe"
    subprocess.run(flags + ["-I", str(root / "main/ekko_shared"), str(root / "tests/test_sd_write_probe.cpp"),
                            "-o", str(storage_binary)], check=True)
    storage = subprocess.run([str(storage_binary), str(temporary)], text=True, capture_output=True, check=True)
    report["storageProbe"] = storage.stdout.strip()
    print(storage.stdout.strip())
report["passed"] = True
args.report.parent.mkdir(parents=True, exist_ok=True)
args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
print(f"Original BDF + actual vendor raster: {report['glyphsRasterCompared']} glyphs, "
      f"{report['actualFactoryEfontGlyphsCompared']} original factory efont glyphs, zero pixel mismatches")
print(f"Fixed UI coverage: {len(labels)} literal lines, {len(characters)} non-ASCII characters, zero missing")
print(f"Menu widths: {report['menuWidths']}")
print(f"Long literal lines requiring application wrapping review: {len(report['literalLinesWiderThan204'])}")
print(args.report)
