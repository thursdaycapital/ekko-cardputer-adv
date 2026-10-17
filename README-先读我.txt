EkkoStudio · Cardputer-Adv v8.3.0-factory1.7.0
Ekko v8.3.0 + 原厂中文 v1.7.0 / Fusion Pixel 12px

双击「安装教程.html」阅读完整离线中文步骤。
只适用于 M5Stack Cardputer-Adv / ESP32-S3 / 8MiB。

A. 已装本项目同布局双固件，保留设置升级：
   firmware/ekko-update-0x10000.bin → 0x10000
   firmware/factory-update-0x340000.bin → 0x340000
   可按需更新一套，或按教程同时更新两套；先核对分区表并备份。
B. 同型号首次安装／明确清空重装：
   firmware/first-install-dual-8MiB-0x0000.bin → 0x0000
   会清除旧设置。空 NVS 整机组合仅离线验证、未实机首启，本次仍为预发布。

原厂「网络设置」成功保存的2.4GHz Wi-Fi供两套固件共用。
「录音回放」：N新录，OK停止保存/回放，G0退出；文件在SD卡/RECORD/。
「红外遥控」：默认B COOLIX；上下选参数，左右调整，OK应用并开机，1开/0关，Tab更多。
空调显示的是设定/发射结果，没有状态回读；更多中的切换指令重复按可能再次切回。
已实测中文、聊天文字/语音、SD两次回放、空调调温/风速/摆风及退出重进保存。
睡眠/强力/屏显/清洁/其他模式及断电保存未逐项实测。

电脑须支持 Agent ACK 和 chatProtocol:1，本机配套版本0.7.36-cardputer.5。
本包不含电脑Studio安装程序；配套补丁见 desktop-support/README.zh-CN.md。
不承诺任意未适配原版Studio直接兼容。首次使用需绑定自己的电脑并配置Agent。

本包不含API Key、Wi-Fi密码、登录凭据、私人整机备份或录音。
远程需电脑开机联网、Studio/Agent可用、官方中继可达并已完成首次绑定。
全部校验见 SHA256SUMS.txt；更新记录见 CHANGELOG.zh-CN.md。
