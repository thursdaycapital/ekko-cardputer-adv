# Arduino 2.0.17／ESP-IDF 4.4.7 SDK notices 补充

2026-10-17。本目录只收集当前候选实际选入 SDK 的原文、来源声明和审计证据，不生成镜像，不判断合并固件的许可。既有发布目录的 92 条记录、原厂 ESP-IDF 5.4.2 资料及候选源码均未改动。

## 已确认的来源链

- 当前候选框架为 `framework-arduinoespressif32 3.20017.241212+sha.dcc1105b`。map 开头的 archive 选入记录涉及 **56 个 ESP32-S3 SDK archive**，其本地 Git blob SHA 全部与官方 Arduino 2.0.17 提交 `5e19e086c43d0fa5e5a596497ff8f11a0a43f6c2` 一致。
- 官方 SDK 更新提交 `fd49a0abca2385a3c074c6ca5b8ad60f8b8c273d` 标题明确记录 `IDF 38eeba213a (#9339)`；固定 IDF v4.4.7 tree 为 `38eeba213aa695aabfd6d89aa9f5078dbe5a94c3`。子模块版本及仓库 URL 取自该 tree 和 `.gitmodules`。
- Wi-Fi 的 6 个 archive、PHY／BTBB 的 2 个、BT controller 的 1 个，以及 IDF 内嵌 Xtensa HAL 的 1 个，另外逐字节核对了对应固定源仓库的 Git blob，共 **10 项匹配**。`libcore.a` 在这条 Wi-Fi 来源链内，已确认，不再列作未知来源。
- map 同时选入当前工具链 `8.4.0+2021r2-patch5` 的 `libc.a`、`libm.a`、`libgcc.a`、`libstdc++.a`。对应随包原文单独保存，未把工具链整体包字段套用到固件。

详见 `link-audit.json`。其中 map／ELF SHA 对应采集时的候选快照。archive 被选入后仍可能有 section 被 `--gc-sections` 删除，因此这不是最终逐 section 的 SBOM；后续重新链接应核对最终 map／ELF。

## 本轮新增的 25 条记录

| 资料 | 保存位置 | 已保留的边界 |
| --- | --- | --- |
| FreeRTOS 原 MIT 文本及 Kernel V10.4.3 版权原头 | `sdk-4.4.7/freertos/` | 实际安装头文件完整 blob 与固定 IDF 匹配；根 MIT 文本没有版权行，原 Amazon 版权注释另存，不能省略。 |
| lwIP、Mbed TLS、cJSON、SPIFFS、protobuf-c 原文 | `sdk-4.4.7/<组件>/` | 固定 IDF 子模块提交；每个下载文件均与子模块 tree 的 Git blob 匹配。Mbed TLS 双许可原文完整保留，不改写选项。 |
| WPA COPYING、含当前 BSD 条款的 README | `sdk-4.4.7/wpa_supplicant/` | COPYING 自身指向 README，不能只保留 COPYING 就称已带完整当前条款。 |
| WPA 实际对象 fastpbkdf2 的原 CC0 声明、官方 CC0 1.0 全文；DES／LibTomCrypt 原始归属节 | 同上 | 这些文件不能统一套用 WPA 根 BSD；仍未穷举其余所有成员的逐文件声明。 |
| IDF Newlib 组件原 COPYING.NEWLIB | `sdk-4.4.7/newlib/` | 与实际工具链内的 COPYING.NEWLIB 字节不同，二者独立保存。 |
| Wi-Fi／PHY／BT controller 原仓库 LICENSE | `sdk-4.4.7/esp32-wifi-lib/`、`esp-phy-lib/`、`esp32c3-bt-lib/` | 固定子模块版本及对应 ESP32-S3 二进制 blob 均匹配；保留原文，不延伸为整机许可结论。 |
| Xtensa HAL 的实际 Cadence 版权及 MIT 原头 | `sdk-4.4.7/xtensa/` | 实际头 blob 与固定 IDF 一致；不把一个头的条款断言为全部 archive 成员的穷尽清单。 |
| esp_diagnostics 实际安装头的 Apache 原声明 | `sdk-4.4.7/esp_diagnostics/` | 与官方 Arduino 固定 tree 匹配，但外部 esp-insights 精确 commit 仍未闭合；这是声明节，不是完整组件许可。 |
| 实际工具链 Newlib／libgloss 随包 notices、GCC GPL v3 与 Runtime Library Exception 3.1、实际运行库头声明、包版本 | `toolchain-8.4.0-2021r2-patch5/` | 只针对实际选入的 4 个 runtime archive 收集相关随包资料；不复制编译器或扩展审查所有主机工具依赖。 |

`source-manifest.sdk.json` 区分原始完整文件、官方许可文本、源码声明节及包元数据，逐条记录 SHA-256、字节数、固定来源／本地包路径和复制行号。25 条不是 25 份独立许可证。13 条是官方原始文件／文本、5 条是 SDK 声明节、7 条是实际工具链原文／声明／元数据。

已有 `supplemental/esp-idf-v4.4.7/LICENSE` 的原文及 SHA 在 manifest 中作为引用保留，没有再次下载或复制。`coverage.json` 将全部 56 个选入 SDK archive 逐项列出：**23 项有新增组件原文或声明资料，另 33 项本轮仅指向既有 IDF 根文本**。这个覆盖分类不代表逐成员 notices 已完整。

## 保留的缺口

以下每项置信度均为 100%，表示证据边界确定，详见 `remaining-gaps.sdk.json`：

- **P2**：`esp_diagnostics` 已确认实际包及头声明，但外部 esp-insights 精确源码提交和完整 notice 集未闭合。
- **P2**：ELF 的 IDF 字符串仍为 `v4.4.7-dirty`；已确认 SDK 二进制和官方 IDF 基线，不宣称所有补丁／构建步骤已还原或可由纯净 tag 重现。
- **P2**：本轮是已定位组件的 notices 补充，未穷举每个选入对象的所有逐文件版权／条款；33 项仅有既有根文本引用。
- **P2**：选入 archive 的快照在链接垃圾回收之前；正式产物仍需核对最终 map／ELF 对应关系，不宣称最终 section 清单已完成。

以上补充不关闭原发布资料中的独立 M5Cardputer LICENSE、GPL／LGPL 对应发布材料、素材授权等其他缺口，也不使用原厂 SDK 5.4.2 替代候选 4.4.7。

## 合并与校验

仅把 `sdk-4.4.7/`、`toolchain-8.4.0-2021r2-patch5/` 复制到发布目录的 `licenses/`，追加 `source-manifest.sdk.json` 的 25 条 `files`；原有 92 条保持不变，全部合并后应为 117 条。`link-audit.json`、`coverage.json`、`source-audit.sdk.json`、`remaining-gaps.sdk.json` 和补充 manifest 是独立审计资料，不计入上述条目数。

`evidence/` 是官方固定版本 tree、提交或子模块配置响应；没有复制二进制 archive、ELF、map 正文、私有 NVS、账号、认证信息、Wi-Fi 设置或运行日志。`SHA256SUMS` 只覆盖本隔离目录；上层发布清单应由最终统一打包步骤更新。
