# Cardputer ADV 原厂菜单中文版

当前版本 **`factory-ekko-v1.7.0-ac`**，基于 M5Stack 官方 [M5Cardputer-UserDemo / CardputerADV](https://github.com/m5stack/M5Cardputer-UserDemo/tree/b549eac0a3c65bc108186c276b8fac0a214aaa4e)，保留原厂应用与 EkkoStudio 双固件入口。它是定制移植版本。

本次发布应用为 3,029,536 字节，SHA-256 为 `d9eb27301696f863041f420a4a2cb76500c54b437414e9823dffce257e71db51`，写入既有双固件布局的 `factory@0x340000`。版本、构建警告和验收范围见 `factory-build-manifest.json` 与发行包的 `evidence/factory-ac-v1.7.0/`。

## 当前功能与验证边界

红外遥控默认使用方案 B / COOLIX24：17～30℃整度，制冷／除湿／自动／制热／送风，自动／低／中／高风速，以及摆风、导风片步进、睡眠、强劲、屏显、清洁等独立动作。用户已确认开关、调温、风速、上下摆风及退出后重进的设定保留；其余模式、附加动作和断电后的设定保留尚未逐项实测。红外没有空调状态回执，切换指令不会显示成已确认的绝对开关。详见 `AC_REMOTE.zh-CN.md`。

保留 v1.6.0 的 FAT32 SD 录音与文件回放、共享 Wi-Fi、开机连接已保存网络、北京时间、中文菜单和 Aa 状态标记。SD 功能的既有操作与实测边界见 `SD_RECORDING.zh-CN.md`；历史验收不等于 v1.7.0 对全部外设重新测试。

14 项菜单、工具页固定帮助/状态/错误、启动操作说明均已中文化，使用 Fusion Pixel 12px 兼容字体。品牌、物理键名、文件名、SSID、用户内容及内部协议标识保留。字体包含原 efont12 的 180 个兼容补字；许可与精确覆盖见 `fonts/fusion-pixel`。

## 构建

使用 ESP-IDF **5.4.2** 与 ESP32-S3 目标。源码已附依赖及本地音频修补，直接使用本目录构建；不要运行 `fetch_repos.py`，也不要重新下载依赖覆盖本地修改。固定来源、依赖与重建步骤见 `SOURCE-REBUILD.zh-CN.md`、`factory-build-manifest.json` 和 `dependencies.lock`。

```sh
idf.py build
```

构建结果 `build/cardputer-adv.bin` 属于 `factory@0x340000`。更新已安装的双固件设备时，按发行包的中文安装教程只写指定的两个 app 地址。普通 `idf.py flash` 还会写启动器、分区表和 OTA 初始数据，不是本次保留现有设置的更新步骤。

## 验证

```sh
python3 tests/run_font_storage_validation.py
python3 tests/run_ac_protocol_tests.py
python3 tests/run_app_remote_tests.py
python3 tests/run_keyboard_tests.py
```

测试使用源码内真实 M5GFX 字体方法与原始 BDF，编译器需支持 C++17、ASan/UBSan；PNG 样张需要 Pillow。该测试验证字体像素/字宽及 POSIX 写读，不代替设备屏幕或 SD、红外、LoRa、GPS 外设测试。

`DUAL_FIRMWARE.zh-CN.md` 保留双固件、共享 Wi-Fi、开机自动连接和北京时间的设计说明。此次应用保留上述行为；新增加的 SD 探测使用独占临时文件，完成后删除，避免覆盖用户的 `test.txt`。

## 上游与致谢

原厂代码及依赖遵循各自许可证，分发时须保留原有声明。主要项目：

- [M5Stack / M5Cardputer-UserDemo](https://github.com/m5stack/M5Cardputer-UserDemo)
- [M5Unified](https://github.com/m5stack/M5Unified)、[M5GFX](https://github.com/m5stack/M5GFX)
- [Fusion Pixel](https://github.com/TakWolf/fusion-pixel-font)
- [PikaPython](https://github.com/pikasTech/PikaPython)、[RadioLib](https://github.com/jgromes/RadioLib)、[raylib](https://github.com/raysan5/raylib)、[TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus)
- [mooncake](https://github.com/Forairaaaaa/mooncake)、[smooth_ui_toolkit](https://github.com/Forairaaaaa/smooth_ui_toolkit)、[mooncake_log](https://github.com/Forairaaaaa/mooncake_log)
- [Adafruit_TCA8418](https://github.com/adafruit/Adafruit_TCA8418)、[esp32s3-keyboard](https://github.com/hhuysqt/esp32s3-keyboard)、[xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)
- [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266/tree/1e2f0f3ef0a93cbf2a8ddb2e95130f8f4c584b3f)：`main/apps/app_remote/ac_protocol.h` 为 LGPL-2.1-only 的便携协议适配；完整许可、原作者署名、固定参考源码与逐文件 SHA 在 `third_party/irremoteesp8266-reference/`。修改该模块后可重编整个原厂 app，见 `SOURCE-REBUILD.zh-CN.md`。

## 历史：录音回放修复（v1.5.1）

当时录音仅保留最近约1秒，现已由 v1.6.0 的 SD 文件录音替代。该历史修复对附带 M5Unified 的 Mic_Class/Speaker_Class 加入生命周期处理；后续 SD 录音继续包含本地队列与音频修补。当前完整代码在 `components/M5Unified/`，配套补丁在 `patches/m5unified-audio-lifecycle.patch`，依赖并非原封未改。

`python3 tests/run_app_record_tests.py` 编译实际应用，`python3 tests/run_record_sdk_tests.py` 提取实际SDK方法进行宿主测试；硬件、RTOS与I2S均有替身，不能替代实机听音和异常恢复验收。
