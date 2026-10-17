# 配套电脑端 Studio 源码补丁

此目录保存本次 Cardputer-Adv 实际使用的电脑端改动。它包含 48 个已跟踪文件的修改和 9 个新增生产源码／测试文件，共 **57 个文件**；无需把原开发仓库的混合改动强行提交。设备板级源码另见 `../source/ekko-cardputer-adv/`，本补丁不重复收录。

这是源码补丁，**不包含电脑 Studio 安装包**。适配的源码版本为 `0.7.36-cardputer.5`，精确上游基线为 `1242ac63556491c92ba6dd49d89fd19c91a6bdd5`。后续官方升级可能覆盖这些改动，应重新合并并验证；不能把任意官方原版 Studio 都视为兼容。

## 包含的功能

- 16 Agent 目录、明确的 Agent ACK、选择版本隔离、`chatProtocol:1`，以及打字发送／历史记录。旧后台没有有效 ACK 时，设备会要求更新后台，不会悄悄切到其他 Agent。
- 支持 scoped 的 MCU Coding Agent 使用当前 Studio Profile 的提供商、模型和协议，避免复用已删除的 CLI 代理配置；Cursor、Qoder 保持其原生 global 模式，仍须配置原生账号。
- Grok scoped TOML 配置隔离和相关运行器兼容修复；运行错误、取消和回合元数据的配套处理。
- 官方设备资格重新核验、列表刷新、数据库状态更新及对应界面提示。仍向官方服务核验真实资格，**不伪造“官方”状态、不绕过官方认证**。
- 原生官方中继载体兼容、登录 JWT 贯穿媒体请求、鉴权轮换／旧音频隔离，以及中继断开后的恢复支持。
- 对应的前后端回归测试和构建入口。完整覆盖路径及每个文件校验见 `source-files.json`。

## 应用到精确基线

需要 Git、Node.js **23 或更高版本**和 npm。以下命令从本发布目录根部执行；使用新的源码目录，保留你原来的开发工程：

```sh
git clone https://github.com/EKKOLearnAI/ekko-studio.git ekko-studio-cardputer
git -C ekko-studio-cardputer checkout 1242ac63556491c92ba6dd49d89fd19c91a6bdd5
git -C ekko-studio-cardputer apply --check ../desktop-support/studio-cardputer-0.7.36-cardputer.5.patch
git -C ekko-studio-cardputer apply ../desktop-support/studio-cardputer-0.7.36-cardputer.5.patch
cd ekko-studio-cardputer
npm ci --ignore-scripts
npm run build
```

`npm ci` 的原生依赖能否直接使用预编译包取决于操作系统和架构；构建出现依赖问题时按上游 DEVELOPMENT.md 处理。本归档不捆绑 node_modules、Hermes Python 环境或各 Agent CLI。

补丁也保留了板级 npm 构建命令；若要使用 `mcu:cardputer:*`，先把本包 `source/ekko-cardputer-adv/` 放到克隆工程的 `packages/esp32-s3/cardputer-adv/`。仅构建电脑端不需要这一步。

## 运行或打包

切换前退出旧 Studio，避免两个实例同时写同一份本机数据。构建后可按上游方式启动：

```sh
node bin/hermes-web-ui.mjs start --port 8648
```

如需生成桌面程序，使用上游的平台构建命令；例如 macOS：

```sh
npm run build:desktop:mac
```

该步骤需要下载上游桌面运行时及平台构建依赖。本包没有替你生成、签名或安装新的桌面程序；当前归档验证只证明补丁可应用且能逐字节还原这 57 个源码文件。

## 使用与兼容边界

先在电脑 Studio 配置并验证相应 Agent、账号和模型，再给 M5 做首次绑定。Wi-Fi 已连接不等于 Studio／模型服务已可用；某个 CLI 安装成功也不代表其原生账号、额度和模型一定可用。

远程需要电脑 Studio 在线、原生中继开启、官方服务认可设备资格及已有有效绑定。修复不会替用户登记设备、获取 API key 或修改官方云服务。设备聊天与语音的原生协议、Agent ACK 和 `chatProtocol:1` 必须由实际运行的电脑端共同支持。

旧的 `cardputer-ekko-remote-v8.2.2`、`cardputer-cu-autoconnect` 目录只保存过部分后端摘录；早期完整源码包也早于官方资格和最新 Agent 配置修复。本目录统一补齐这些电脑端源码，不能用早期设备固件包代替。

本目录不含本机 Profile 配置、认证资料、数据库、设备绑定资料、日志、真实私网地址或 Wi-Fi 名称。测试中的虚构 token 和局域网地址只是回归输入，不是可使用的凭据。

## 验证与许可

已在临时基线副本执行 `git apply --check`，实际应用后，57 个文件与开发工程逐字节一致；验证副本已清理，原仓库未修改，也未提交或推送。详情见 `patch-validation.json`。这不替代新机器构建、安装、空白设备首启或跨网实测。

补丁遵循上游 **Business Source License 1.1**；完整原文见 `LICENSE-BSL-1.1` 和发布目录根部 `LICENSE`。保留非商业 Additional Use Grant、需另行许可的商业使用条款、2029-05-10 变更日期及其后 Apache License 2.0 条款。上游商标、头像与其他第三方资源仍遵循各自许可；此补丁不表示官方发布或背书。
