# EkkoStudio for M5Stack Cardputer-Adv

基于 Ekko Studio 原生 ESP32 固件的 **Cardputer-Adv / ESP32-S3 / 8MiB** 双固件适配：中文原厂菜单与 EkkoStudio 共存，16 个 Agent、键盘中文聊天、Fn＋空格语音、原生设备管理和远程连接；原厂工具新增 SD WAV 录音与美的空调遥控。这是社区适配，非 M5Stack 或 Ekko 官方发布的设备固件。

当前发布 **v8.3.0-factory1.7.0**：Ekko v8.3.0 ＋ 原厂中文 v1.7.0，基础字形为 Fusion Pixel 12px，空调温度放大显示。现机两套应用已完整读回，用户确认中文、聊天文字与语音顺畅、SD 保存后两次回放，以及空调调温／风速／摆风和退出重进偏好正常。

**本次仍是预发布。空 NVS 的完整 8MiB 首刷组合仅通过离线验证，尚未在空白设备上首启实测。** 已验收的保留设置应用更新，与未验收的整机首启分别记录。

## 下载与安装

[Release 下载](https://github.com/thursdaycapital/ekko-cardputer-adv/releases/tag/v8.3.0-factory1.7.0) · [中文安装教程](安装教程.zh-CN.md) · [离线图文教程](安装教程.html) · [更新记录](CHANGELOG.zh-CN.md)

| 用途 | 文件 | 地址 | 设置 |
|---|---|---|---|
| 已装本项目同布局双固件，更新 Ekko | `firmware/ekko-update-0x10000.bin` | `0x10000` | 保留 |
| 同布局更新原厂中文、SD 录音、空调遥控 | `firmware/factory-update-0x340000.bin` | `0x340000` | 保留 |
| 同型号首次安装／明确清空重装 | `firmware/first-install-dual-8MiB-0x0000.bin` | `0x0000` | 清除 |

想同时更新两套应用时，按教程一次写入两个 app，再分别校验。已有设备先备份，核对分区表；不要把 app 更新文件当作 0 地址完整镜像。

## 原厂工具与日常操作

- **网络设置：**中文列表，R 重扫、M 手动输入网络名；连接成功后两套固件共享保存的 2.4GHz Wi-Fi。
- **SD 录音：**「录音回放」中 N 新录、OK 停止保存／回放，G0 返回；WAV 位于 `/RECORD/`。固定 24KiB PCM 缓冲，录音文件随 SD 保存，不把整段音频放在 RAM。
- **空调遥控：**默认 B COOLIX，17～30℃、五模式、四档风速；上下选项、左右调整、OK 应用并开机，1 开／0 关，Tab 更多。切换类命令与屏幕设定**没有空调状态回读**。
- **Ekko 聊天：**Agent 页方向键选择、OK 进入；Ctrl＋空格切中英文，Fn＋空格说话，Tab 看历史，Esc 返回。

睡眠／强力／屏显／清洁及其余空调模式没有逐项实机验收；断电后的录音与空调偏好恢复尚未另做实体测试。具体按键和边界见[教程](安装教程.zh-CN.md)。

## 电脑端与远程要求

本仓库不包含电脑 Studio 安装程序。当前 16 Agent 路由与打字聊天使用 Agent ACK 和 `chatProtocol:1` 扩展，已验收环境为 **`0.7.36-cardputer.5`**；新增配套补丁见 [desktop-support](desktop-support/README.zh-CN.md)，**不承诺任意未适配的原版 Studio 直接兼容**。各 Agent 仍需在电脑配置好 CLI、账号、API Key 和可用模型。

远程复用官方中继：电脑保持开机联网、Studio／Agent 可用、中继已开启，并完成一次远程入口绑定／Profile 保存。以后换到可访问中继的 2.4GHz 网络，可复用绑定自动恢复。已有手机热点跨网文字、语音、重启及短暂断网恢复实测；某些宽带线路存在中继超时。公共 Wi-Fi 网页认证、账号凭据撤销等仍需相应处理，不能保证任意网络可用。

## 源码与构建

- [Ekko 板级源码](source/ekko-cardputer-adv/)，PlatformIO 固定依赖。
- [原厂中文源码](source/factory-cardputer-adv/)，独立 ESP-IDF 5.4.2 app。
- [Arduino 对应库](source/arduino-esp32-2.0.17/)、[构建与重新链接说明](RELINK.md)。
- [电脑端补丁说明](desktop-support/README.zh-CN.md)、[第三方许可](THIRD_PARTY_NOTICES.md)、[隐私说明](docs/PRIVACY.md)、[离线组装证据](evidence/assembly-inputs.json)。

```sh
uvx --from platformio pio run -d source/ekko-cardputer-adv -e cardputer-adv
```

重建的 Ekko app 写入 `0x10000`，原厂 app 写入 `0x340000`。不要直接运行原厂工程的 `idf.py flash` 替代本项目双固件安装流程。

## 文件校验

| 应用 | 字节数 | SHA256 |
|---|---:|---|
| Ekko v8.3.0 | 3,300,528 | `c5898d0e79d600aec884c5be7c2e08a15a3301bc6a983b19ed5cd389546e7448` |
| 原厂中文 v1.7.0 | 3,029,536 | `d9eb27301696f863041f420a4a2cb76500c54b437414e9823dffce257e71db51` |

完整首刷镜像及全包文件的大小／校验值见 [firmware-manifest.json](firmware-manifest.json) 和 [SHA256SUMS.txt](SHA256SUMS.txt)。本包不含 Wi-Fi 密码、登录凭据、API Key、私人整机备份或录音。

![历史 Fusion12 聊天布局参考，非实拍](preview/Fusion12-chat-preview.png)

上图为旧版历史布局参考，由真实字形与绘制指令生成，非设备照片，不代表本次全部页面。

## 来源与许可

Ekko 基于 [EKKOLearnAI/ekko-studio](https://github.com/EKKOLearnAI/ekko-studio)，原厂基于 [M5Cardputer-UserDemo](https://github.com/m5stack/M5Cardputer-UserDemo) 的 CardputerADV 分支。Ekko 来源为 BSL1.1，保留其非商业 Additional Use Grant 与变更条款；独立原厂应用、红外适配、库和字体遵循各自许可，不以单一 MIT 覆盖。详见 [LICENSE](LICENSE) 与 [第三方许可说明](THIRD_PARTY_NOTICES.md)。商标、头像及素材来源见对应 manifest，不表示上游背书。
