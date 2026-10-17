# 发布许可与来源资料

2026-10-17。`source-manifest.json` 现有 **117 条原文与来源记录**：原有80条、此前12条和本轮SDK／工具链25条均已核验。记录包括完整许可文件、源码原始注释节、资产来源及版本资料；这个数量不等于117份独立许可证。本目录只保留可核对的原文和归属记录，不判断合并镜像的许可，也不改变组件或品牌素材的授权边界。

## 本轮补入的原文

| 资料 | 保存位置 | 范围 |
| --- | --- | --- |
| GNU GPL v3 全文 | `supplemental/gnu/GPL-3.0.txt` | GNU 官方固定版本文本；对应原厂 keymap.h 的 GPL-3.0-or-later 声明。原声明保留，不把“or later”改成“only”。 |
| GNU LGPL v2.1 全文 | `supplemental/gnu/LGPL-2.1.txt` | GNU 官方固定版本文本；对应 TinyGPS++／Arduino 既有 LGPL-2.1-or-later 声明。 |
| Arduino ESP32 原仓库全文 | `supplemental/arduino-esp32-2.0.17/LICENSE.md` | 2.0.17 commit `5e19e086…` 的原文件，对应本机 PlatformIO Arduino 包；不覆盖内嵌第三方组件。 |
| LovyanGFX 引用全文 | `supplemental/LovyanGFX/license.txt` | 官方原链接许可文件的固定 commit `b62ea8b8…`；含 Adafruit/Bodmer/lovyan03 原始声明和完整 BSD/FreeBSD 条款。 |
| TFT 字体引用全文 | `supplemental/TFT_eSPI/license.2023.txt` | 原链接对应官方仓库 `d506a3c4…` 的完整历史文件；保留原版权年及全部声明。 |
| Adafruit TCA8418 全文 | `supplemental/Adafruit_TCA8418/license.txt` | 官方许可文件 commit `d38d9e27…`，保留 Limor Fried 原版权及 BSD 条款；实际驱动是本地适配副本。 |
| 候选 SDK 根许可 | `supplemental/esp-idf-v4.4.7/LICENSE` | 真实 4.4.7 标签的根文件；**不是**预编译 SDK 组件级完整清单。 |
| 3 项头像来源仓库许可 | `supplemental/assets/` | Hermes、Kimi 用原 manifest 固定 commit；Octicons 已将浮动引用固定到 `6dbe38e4…`，并核对 Copilot SVG SHA 与现存 manifest 完全一致。仓库许可不等于商标许可。 |
| TomThumb 完整原始声明 | `supplemental/candidate/`、`supplemental/factory/` | 各自锁定版本源文件第 1–47 行原字节，包含完整 BSD 3 条款和修改来源；没有复制字模。 |

`source-manifest.json` 每项保留真实 SHA-256、字节数、来源 URL 或本机相对路径、版本/commit、原始引用和适用边界。完整文件按原字节复制。`.notice.txt` 是源码注释节；本轮 TomThumb 节内确实包含完整条款，不能据此把其它只有 SPDX 的 `.notice.txt` 一并称为全文。

## 仍需保留的缺口

| 级别 / 置信度 | 项目 | 当前状态 |
| --- | --- | --- |
| P2 / 100% | M5Cardputer 1.1.1、原厂主仓独立 MIT 文件 | 官方固定版本递归全树也未找到常见名称的独立 LICENSE/COPYING 文件，不只是本地漏包；已有 2025 M5Stack MIT SPDX 原头保留，不挪用其它组件的版权行填补。检索范围和固定提交见 `source-audit.json`。 |
| P2 / 100% | GPL/LGPL 对应发布材料 | 正文已补；最终对应源码、修改、构建/链接与替换材料仍需按最终镜像核对。 |
| P2 / 100% | Arduino 内嵌 ESP-IDF 4.4.7 组件 notices | 新增25条实际组件／工具链原文及声明；56个archive来源匹配，23项有组件资料、33项仍仅根文本。外部esp_diagnostics、dirty补丁、逐成员声明及最终GC快照四子项仍缺，见sdk-audit。 |
| P3 / 100% | Lovyan/TFT/Adafruit 原始导入版本 | 官方引用全文已补；原嵌入代码的准确导入 commit 未声明，不能把许可文件 commit 当作代码版本。 |
| P3 / 100% | 头像、启动视频归属 | 3 项仓库许可正文已补；其余沿用资产 manifest 的来源和授权边界，未新建立素材特定授权或品牌背书。 |

详细结构化记录见 `remaining-gaps.json`。M5Unified、M5GFX、IRremote、ArduinoJson MIT，U8g2、efont、GFXFF/GLCD，PikaScript、fmt、minimp3 等已有原文保持原版本与 copyright。代码声明、许可正文、素材来源是三类记录，彼此不能替代。

## 合并与核验记录

本轮 12 条已并入主 `source-manifest.json`；原 80 条记录值和文件字节均保持不变。合并后 92 条记录的 SHA-256 与字节数逐项核对通过。`source-manifest.supplement.json` 保留合并前冻结的补充元数据和基线 manifest SHA，主 manifest 的 `supplement_merge` 记录本次追加与校验情况。

`source-audit.json` 记录两份固定上游全树检查和 Octicons 固定化证据；它是检索元数据，不是许可正文。`remaining-gaps.json` 区分许可正文、原来源声明、对应发布材料和素材归属。本目录不生成单独的校验清单，最终发布目录校验值由统一打包步骤更新。

只复制许可与来源白名单资料；没有复制 NVS、整机读回、私有运行目录、账号、密码、认证数据或目标设备码。本次合并没有生成二进制、公开发布或烧录。


## 本轮SDK／工具链补充（117条当前状态）

新增25条由实际候选map／ELF与固定官方版本定位：13条官方原始文件／文本、5条SDK声明节、7条实际工具链资料。原92条记录和原文件字节保持不变，117条逐项SHA与长度全部通过；SDK审计对应当前候选map／ELF，内部44项SHA也通过。

当前选入的56个SDK archive与固定官方Arduino blob全部匹配；Wi-Fi／PHY／BT／Xtensa HAL另10项与固定IDF来源匹配。组件原文包括FreeRTOS、lwIP、MbedTLS、cJSON、SPIFFS、protobuf-c、WPA／相关加密声明及无线库，实际工具链newlib/GCC/runtime条款单独保留，不复制二进制archive。

冻结的补充资料完整保存在 `sdk-audit/`；25条主manifest记录的真实文件还位于顶层 `sdk-4.4.7/` 和 `toolchain-8.4.0-2021r2-patch5/`。保留原目录的SHA清单和证据树，原文与审计分开，不因声明数量增加就称完整SBOM。补充覆盖与全部四项P2/100%边界见 `sdk-audit/README.zh-CN.md`、`coverage.json`、`remaining-gaps.sdk.json`。旧的80→92合并记录是历史，不是当前总数。

原厂对应源码压缩包及18文件修改patch、固定依赖、全量构建材料已另外提供；最终GPL/LGPL链接与替换材料仍按最终镜像核对，不在本轮提前关闭。
