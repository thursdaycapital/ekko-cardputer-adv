# 原厂 v1.7.0：源码、修改与重新链接

这份目录对应发布的 `factory-ekko-v1.7.0-ac` 应用，包含应用、板级驱动、字体、测试、完整依赖源码、构建配置及 IRremoteESP8266 协议适配。它负责原厂菜单及 EkkoStudio 入口；EkkoStudio 是另一套独立应用，源码见相邻的 `../ekko-cardputer-adv/`。

## 固定来源与保留材料

- 原厂基线：`m5stack/M5Cardputer-UserDemo` 的 CardputerADV 分支，提交 `b549eac0a3c65bc108186c276b8fac0a214aaa4e`。完整修改后的源码直接在本目录；旧版的 18 文件补丁不代表 v1.7.0 的完整改动。
- SDK：ESP-IDF **5.4.2**，提交 `f5c3654a1c2d2a01f7f67def7a0dc48e691f63c0`，由官方仓库安装，包括 esp32s3 工具链和匹配的 Python 环境。本包不附 SDK 或主机工具链。
- `components/` 是已经保存的依赖源码。基线标签为 M5GFX 0.2.15、M5Unified 0.2.10、mooncake v2.2.0、mooncake_log v1.3.0、smooth_ui_toolkit v2.4.0。M5Unified 有本地音频生命周期及队列修补，完整修改版与 `patches/m5unified-audio-lifecycle.patch` 均保留；不要用同标签原版覆盖。
- `managed_components/` 完整保留；版本和组件哈希以 `dependencies.lock` 为准。保留 `sdkconfig`、`sdkconfig.defaults`、`partitions.csv` 和全部 CMake 文件。
- `main/apps/app_remote/ac_protocol.h` 是 LGPL-2.1-only 的可修改便携库。来源固定为 IRremoteESP8266 提交 `1e2f0f3ef0a93cbf2a8ddb2e95130f8f4c584b3f`，原始参考、许可证全文和 SHA 在 `third_party/irremoteesp8266-reference/`。
- 保留文件内原始署名及 `fonts/fusion-pixel/` 的 OFL、efont 条款与来源。发行包根 `LICENSE` 对应 Ekko 的 BSL 条款，不替代原厂文件及各依赖的许可证；完整许可集合见发行包根 `licenses/` 和 `THIRD_PARTY_NOTICES.md`。

此发布副本不含 `.git`。**不要执行 `python3 fetch_repos.py`**：它会对已经包含、没有 Git 元数据的依赖调用 `git fetch`；重新获取依赖也可能丢失本地音频修补。本目录的依赖已齐全。

## 构建与修改

先安装并激活上述官方 ESP-IDF 5.4.2 及其 esp32s3 工具链。在本目录执行：

```sh
. /path/to/esp-idf-v5.4.2/export.sh
idf.py -B build-relinked build
```

可以修改 `ac_protocol.h`、TinyGPS++ 或其他可修改库，然后使用同一命令重新编译和链接整个应用；配套应用源码也已提供。新的 ELF、map 和 app 位于 `build-relinked/`。无需使用预编译的原厂 app，项目未增加禁止为调试库修改而逆向分析、重新链接或替换库的限制。各文件原有许可条件继续适用。

普通 SDK/工具依赖在首次安装时需要联网；这不是完整的离线工具链包。路径、编译时刻、ELF 哈希和文档修改会影响重建信息，不承诺重建得到逐字节相同的发布镜像。发布说明和协议头日期注释在验收后补充，已验收的 app 没有重新构建或改写；素材及 SDK 示例的发布脱敏另见发行包根的隐私与验证说明。

可在本目录运行配套主机回归：

```sh
python3 tests/run_ac_protocol_tests.py
python3 tests/run_app_remote_tests.py
python3 tests/run_keyboard_tests.py
```

协议回归提取实际固定上游函数，逐段比较完整波形；UI/键盘回归编译实际应用或驱动并使用硬件替身。它们不能代替实体发射波形、空调响应、LCD、SD 或听音验收。

## 双固件与验收边界

重建的 `build-relinked/cardputer-adv.bin` 只属于 `factory@0x340000`，分区容量为 `0x400000`。Ekko 属于 `app0@0x10000`，容量 `0x330000`；NVS 为 `0x9000/0x5000`，OTA 为 `0xe000/0x2000`，SPIFFS 为 `0x740000/0xc0000`。以发行包的安装教程选择首刷或保留设置的更新流程。

**不要直接执行此工程的 `idf.py flash` 或整套 `flash_args`。** 它们还包含 SDK 5.4.2 启动器、分区表和 OTA 初始数据，不是既有双固件的应用更新步骤。发行包首次安装整机镜像使用单独核对的公共输入组装，见 `../../evidence/assembly-inputs.json`；没有使用私人 flash/NVS 备份。

发布 app 为 3,029,536 字节，SHA-256 `d9eb27301696f863041f420a4a2cb76500c54b437414e9823dffce257e71db51`。主机测试、烧录读回与用户实测分别保存在 `../../evidence/factory-ac-v1.7.0/`。调温、风速、上下摆风和退出重进保留设置已有用户确认；其他模式、额外动作与断电后保留尚未逐项验收。完整空白 NVS 首次开机组合尚未实机验证，不能用构建、合并镜像检查或历史运行替代。
