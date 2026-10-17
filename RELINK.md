# Build, modify and relink

This release pairs Ekko `cardputer-adv-v8.3.0-zh-fusion` and factory `factory-ekko-v1.7.0-ac` as independent ESP32-S3 applications. Use the supplied full modified source and fixed dependencies to rebuild, debug, modify or replace applicable libraries under their own terms. This project adds no prohibition on modifying the LGPL components or reverse engineering for debugging such modifications.

## Ekko application

From the repository root, install uv/PlatformIO and run:

```sh
uvx --from platformio pio run -d source/ekko-cardputer-adv -e cardputer-adv
```

Pinned platform: espressif32@7.0.1; Arduino framework`3.20017.241212+sha.dcc1105b` (Arduino2.0.17), compiler8.4.0+2021r2-patch5. `platformio.ini` fixes all project libraries. Standard registry dependencies and toolchains are downloaded on first build; this is not a fully offline toolchain bundle.

`source/arduino-esp32-2.0.17/` contains the actual framework cores, libraries, variants and PlatformIO build script from the pinned package. The SDK precompiled archives and host toolchain are obtained from that exact normal PlatformIO package, not replaced by factory IDF5.4.2. To modify/relink Arduino LGPL code, use these sources in a separate copy of the complete installed pinned framework, retain its tools/SDK, apply your library/core changes, and select that local framework through PlatformIO `platform_packages`. Do not overwrite another project's shared framework installation. PlatformIO local-package guidance: https://docs.platformio.org/en/latest/projectconf/sections/env/options/platform/platform_packages.html

Build objects/ELF/map and the app image are generated under`source/ekko-cardputer-adv/.pio/build/cardputer-adv/`. After any library change, perform a clean rebuild and run the chat/policy/render tests from that source directory. Flash only the resulting app at`0x10000` in the established dual layout; see the installation guide. Keep the LGPL notices. This project does not add a prohibition on debugging modifications to, or relinking/replacement of, the LGPL libraries.

## Factory application

The current complete factory source snapshot, vendored dependencies, lock, config and local M5Unified audio patch are under `source/factory-cardputer-adv/`. Use its `SOURCE-REBUILD.zh-CN.md` with ESP-IDF **5.4.2**, commit `f5c3654a1c2d2a01f7f67def7a0dc48e691f63c0`, and the matching esp32s3 tools/Python environment. From that source directory:

```sh
. /path/to/esp-idf-v5.4.2/export.sh
idf.py -B build-relinked build
```

Do not run `fetch_repos.py` on this archive: vendored dependencies have no Git metadata and include local patches that must be retained. `build-relinked/cardputer-adv.bin` belongs at **`0x340000`** with capacity `0x400000`. Do not flash this project's bootloader/OTA arguments as the entire paired system. The existing Ekko app belongs at `0x10000` with capacity `0x330000`. Original SDK/toolchain notices are in `licenses/`.

The factory app includes the LGPL-2.1-only portable library `main/apps/app_remote/ac_protocol.h`, adapted from IRremoteESP8266, and LGPL-covered TinyGPS++ code. The complete library modifications and full application source are included. Modify the portable header or other covered library, then rebuild with the command above to generate a replacement executable/ELF/map containing your modified library. No prebuilt factory object files are required for this full-source route. Keep each library's original author notices and applicable license; the complete IRremoteESP8266 LGPL text and pinned reference sources are in `third_party/irremoteesp8266-reference/`. The root BSL license for the separate Ekko work does not replace these notices or restrict modifications to these covered libraries.

The shared partition table, public bootloader input and app images are included for offline verification, with sizes and hashes in `evidence/assembly-inputs.json`. The merged clean image is 8 MiB with blank NVS/otadata/SPIFFS; private device backups and credentials are not included. Blank-NVS first boot has not been physically accepted. Runtime, user-observed behavior and host tests are separate evidence. Release-only documentation and the protocol-header date comment were corrected without changing runtime logic or rebuilding the accepted app images. Different paths, build dates and ELF hashes can change rebuilt bytes; byte-for-byte reproducibility is not promised.
