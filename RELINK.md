# Build, modify and relink

The shipped Ekko app is the accepted ESP32-S3 build, not the experimental stock-protocol candidate. It is not cryptographically locked to this release. Use the supplied source and fixed PlatformIO dependencies to rebuild, debug, modify or replace applicable libraries under their own terms.

## Ekko application

From the repository root, install uv/PlatformIO and run:

```sh
uvx --from platformio pio run -d source/ekko-cardputer-adv -e cardputer-adv
```

Pinned platform: espressif32@7.0.1; Arduino framework`3.20017.241212+sha.dcc1105b` (Arduino2.0.17), compiler8.4.0+2021r2-patch5. `platformio.ini` fixes all project libraries. Standard registry dependencies and toolchains are downloaded on first build; this is not a fully offline toolchain bundle.

`source/arduino-esp32-2.0.17/` contains the actual framework cores, libraries, variants and PlatformIO build script from the pinned package. The SDK precompiled archives and host toolchain are obtained from that exact normal PlatformIO package, not replaced by factory IDF5.4.2. To modify/relink Arduino LGPL code, use these sources in a separate copy of the complete installed pinned framework, retain its tools/SDK, apply your library/core changes, and select that local framework through PlatformIO `platform_packages`. Do not overwrite another project's shared framework installation. PlatformIO local-package guidance: https://docs.platformio.org/en/latest/projectconf/sections/env/options/platform/platform_packages.html

Build objects/ELF/map and the app image are generated under`source/ekko-cardputer-adv/.pio/build/cardputer-adv/`. After any library change, perform a clean rebuild and run the chat/policy/render tests from that source directory. Flash only the resulting app at`0x10000` in the established dual layout; see the installation guide. Keep the LGPL notices. This project does not add a prohibition on debugging modifications to, or relinking/replacement of, the LGPL libraries.

## Factory application

The complete factory source snapshot, dependencies/lock, config and tracked patch are included under`source/factory-cardputer-adv/`. Use its`SOURCE-REBUILD.zh-CN.md` and fixed ESP-IDF5.4.2 environment to build only the app. Rebuilt factory app belongs at`0x340000`; do not flash that project's bootloader/OTA args as the entire paired system. Original SDK/toolchain notices are in`licenses/`.

The shared partition table, bootloader inputs and app images are included for offline verification. The full merged clean image is8MiB with emptyNVS/otadata/SPIFFS. Personal device flash backups and credentials are not included.
