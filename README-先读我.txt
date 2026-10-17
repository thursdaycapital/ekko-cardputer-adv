EkkoStudio · Cardputer-Adv v8.2.4.1 安装包

双击「安装教程.html」阅读完整中文步骤。
开发样机上的本版应用已验收；其他设备请按自己的状态选择安装方式。

A. 已装本项目双固件、保留设置升级：
   firmware/ekko-update-0x10000.bin → 0x10000
B. 同型号首次安装／明确清空重装：
   firmware/first-install-dual-8MiB-0x0000.bin → 0x0000
   会清除旧设置；新空NVS首启镜像仅离线检查，尚未实机首刷验收。

只适用于 M5Stack Cardputer-Adv / ESP32-S3 /8MiB。
先备份，选对方式，按教程识别自己的串口。
首次新机配网后需绑定自己的配套Studio；保留设置升级不需重复绑定。
电脑须支持MCU Agent确认与chatProtocol:1；本机配套版本0.7.36-cardputer.5。
本包不含电脑Studio安装程序、API Key、Wi-Fi密码或私人备份。

所有文件校验：SHA256SUMS.txt。
