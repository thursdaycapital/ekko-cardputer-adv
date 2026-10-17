# 原厂菜单部分：固定源码与重建说明

这份目录是 M5Stack Cardputer ADV UserDemo 的适配源码，负责原厂应用、`EkkoStudio` 入口、共享 Wi-Fi 和北京时间。它不是 Ekko 的 Arduino 应用，也不是完整 8 MiB 安装镜像。只在 work 中准备，尚未发布、未向设备写入。

## 固定来源

- 项目：`https://github.com/m5stack/M5Cardputer-UserDemo.git`，`CardputerADV` 基线 `b549eac0a3c65bc108186c276b8fac0a214aaa4e`。
- 原基线的 18 个 tracked 文件改动见 `UPSTREAM-CHANGES.patch`；新增原厂 Ekko 应用、共享存储、诊断和测试已直接包含。
- 依赖源码直接包含在 `components/`、`managed_components/`，各自许可保留，具体版本/提交见 `SOURCE-PROVENANCE.json` 和 `dependencies.lock`。
- ESP-IDF **5.4.2**，提交 `f5c3654a1c2d2a01f7f67def7a0dc48e691f63c0`，SDK/工具链不包含在此目录，使用官方安装。不能用候选 Ekko 的 Arduino/ESP-IDF 4.4.7 工具链代替。
- 此副本不含 `.git`；**不要运行原始 README 的 `fetch_repos.py`**。它会对已包含的无 Git 元数据依赖尝试 `git fetch`，而且 `repos.json` 是 tag 名；本副本的完整固定依赖源码已齐全。需要从网上重新获取时，应按 `SOURCE-PROVENANCE.json` 的完整 commit 核对，而不只依赖可变 tag。

## 标准构建

先安装/激活上述官方 ESP-IDF 5.4.2 的 esp32s3 工具链与其 Python 虚拟环境，然后在本目录执行：

```sh
. /path/to/esp-idf-v5.4.2/export.sh
idf.py build
```

本机系统 Python 为 3.14，已有 IDF 环境为 3.12，因此本次实际采用以下环境后运行标准构建；其他电脑应换成本机 SDK/工具路径，不能照抄绝对路径：

```sh
export IDF_PATH=<WORKSPACE>/work/esp-idf-v5.4.2
export IDF_TOOLS_PATH=<WORKSPACE>/work/idf542-tools
export IDF_PYTHON_ENV_PATH="$IDF_TOOLS_PATH/python_env/idf5.4_py3.12_env"
. "$IDF_PYTHON_ENV_PATH/bin/activate"
. "$IDF_PATH/export.sh"
idf.py -B <WORKSPACE>/work/ekko-stock-compat-20261017/factory-release-build build
```

这里使用干净的独立 build 目录完成全量构建，不复用原工程旧 object。`sdkconfig` 是不含运行时凭据的完整编译配置，刻意保留以固定开关；`sdkconfig.defaults` 是原工程最小配置。编译包含时间/ELF 哈希，路径/构建时刻改变后不承诺 app SHA 逐字节相同；构建可成功与可重现相同二进制是两件事。

## 双固件组装边界

`cardputer-adv.bin` 是 factory app，写入地址 `0x340000`，容量上限 `0x400000`。其分区表与 Ekko 一致：`nvs 0x9000/0x5000`、`otadata 0xe000/0x2000`、`app0 0x10000/0x330000`、`factory 0x340000/0x400000`、`spiffs 0x740000/0xc0000`。

**不要直接执行这里的 `idf.py flash` 或生成的整套 `flash_args`。** 它们包含 ESP-IDF 5.4.2 bootloader 和 OTA 初始选择，不是历史双固件组合。完整包将使用 Ekko/PIO 公开可重建的 IDF 4.4.7 bootloader，再独立组装已核对的分区与两应用。禁止把私有设备 flash/NVS 拷贝进通用镜像；新设备与已安装设备的升级/保留设置流程还需分别验收。

## 源码已确认的接口

- `main/apps/app_ekko/app_ekko.cpp` 菜单名固定为 `EkkoStudio`；选中不自动进入，按 Enter 后验证 app0 地址/大小、`arduino-lib-builder` 描述与完整 ESP image，保存设置后重启。
- 共享 `cardputer/ekko_wifi_v1` 是固定 104 字节独立协议；本版与候选 Ekko 的 `SharedWifiConfig.h` 逐字节相同。只在真正连接成功后单 blob/单 commit 保存；损坏记录不会默默回退至旧凭据。驱动本身使用 RAM storage，不共享跨 SDK 私有 Wi-Fi blob。
- 原厂开机只自动连接已保存网络一次；失败保留配置。此原厂界面的 Wi-Fi 意外掉线不会持续重连，ESP-NOW 例程主动断网后也不自动抢占恢复。
- `TZ=CST-8`、`localtime_r` 显示北京时间。默认字体是原厂 `fonts::efontCN_16`、字号 1（16 像素）。
- 原厂 G0 返回原厂菜单；Ekko 里的 Esc 返回由候选固件的 `FactoryFirmware.cpp`/`returnToFactoryMenu()` 实现，不在本目录里冒称已验证。
- 本轮只做源码、主机测试和构建；没有 USB、刷写、LCD/键盘/音频验收。历史设备确认仅保存在原目录旧记录，不覆盖新候选的实机需求。

## 许可边界

已有各依赖许可与嵌入声明已保留。上游工程根目录未提供单独 LICENSE，部分自有文件标 MIT SPDX，但 PikaScript/字体等导入代码必须分别核查，不能将整个包概括成 MIT。此材料不作最终分发许可已清理完毕的声明。
