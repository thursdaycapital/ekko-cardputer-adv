# Cardputer-Adv 原厂菜单 + Ekko

基于 M5Stack `M5Cardputer-UserDemo` 的 `CardputerADV` 分支，固定提交
`b549eac0a3c65bc108186c276b8fac0a214aaa4e`。保留原厂应用，在菜单第一项增加
`EkkoStudio`；开机先尝试一次已保存网络，再显示原厂开机画面/菜单。选择 EkkoStudio 按 Enter 后验证另一分区的镜像并重启进入 Ekko。
这份源码是适配版，不是未经修改的 M5Stack 发布固件。

## 共存布局

| 分区 | 起点 | 大小 | 用途 |
| --- | --- | --- | --- |
| nvs | 0x9000 | 0x5000 | 保留设置与 Ekko 登录，共享 Wi-Fi |
| otadata | 0xe000 | 0x2000 | 启动选择 |
| app0 / ota_0 | 0x10000 | 0x330000 | Ekko |
| factory | 0x340000 | 0x400000 | 这份原厂菜单固件 |
| spiffs | 0x740000 | 0xc0000 | 保留数据区域 |

最终安装包保留 Ekko 既有 ESP-IDF 4.4.7 启动器。这里仍用官方要求的
ESP-IDF 5.4.2 编译 app；DIO / 80 MHz、HPM 禁用、BBPLL 启动校准开启、
PHY init 数据内置。**不要直接运行这份工程输出的 `idf.py flash`，以免把
5.4.2 启动器和空 OTA 选择一起写入现有设备。使用整合交付包的安装说明。**

## 开机自动连接

启动时从既有共享配置 `FactoryWifi::load` 读取网络，在原厂 Logo/菜单之前尝试连接一次，显示「正在连接已保存网络」。没有有效记录就跳过；失败显示「暂未连接，可在 SetWiFi 更换网络」，随后继续原厂界面，不擦除或改写网络记录。用户已明确选择此开机自动连接方式。

原厂 Logo 等待任意键继续的行为保留；自动连接在它等待按键之前完成。看到 Logo 时可按任意键继续菜单。没有另建后台连接任务，不循环抢占射频；后续意外掉线不会持续自动重连，原厂 ESP-NOW Chat 主动断网后也不自动恢复。需要时进入 SetWiFi 重新连接，或切换网络。

## 设置 Wi-Fi

进入 `SetWiFi` 后单次扫描附近网络。单按 `;`/`,` 选上一个，`.`/`/` 选下一个，
也支持 Fn+方向键；按 OK 进入密码页。已保存网络自动带入自己的密码，换选
其他 SSID 则密码留空。输入后按 OK，看到「已连接并保存」后按 OK 或 G0 返回
菜单，再进入 `EkkoStudio` 会使用刚保存的网络。失败不会替换原来的保存记录。

`R` 手动重新扫描，列表不会在操作中自动刷新跳动；`M` 手动输入隐藏网络名称。
输入页 `Tab` 清空整行、删除键逐字删除，标点和大小写保留；密码始终显示星号。
开放网络密码可留空。`Fn+左上角 Esc` 返回网络列表，顶部 G0 返回原厂菜单。
支持 2.4GHz 个人 Wi-Fi；酒店等浏览器认证网络没有登录门户页面。

输入区域固定坐标、关闭换行/滚动，过长输入只显示末尾；光标闪烁不会移动 SSID。
扫描最多读取 64 个热点，同名网络保留最强信号；普通列表每页显示三个网络。

共享记录位于默认 NVS 的 `cardputer/ekko_wifi_v1`，固定 104 字节，格式详见
`main/ekko_shared/SharedWifiConfig.h`。两个固件使用 RAM 型 Wi-Fi 驱动设置，
不把跨 IDF 版本的私有驱动 blob 当作协议。已有 `wifi_ssid` /
`wifi_password` 旧键只保留供迁移，不再分别写入新凭据；读取顺序为共享记录、原厂旧键、首次导入 Ekko 的
`net/ssid` 和 `net/pass`。仅在实际连接成功后发布记录，不会因为读到旧设置就
覆盖较新的共享网络。已有共享记录如果损坏则停止回退，要求重新配网，避免
悄悄连接过期的网络。

密码在配网页以星号显示，不写入日志。NVS 初始化失败时保留数据并禁用相关
功能，不自动擦除整块 NVS。

## 构建

当前发布源码为 `factory-ekko-v1.7.0-ac`；本文件同时保留较早双固件设计记录。完整当前重建说明见 `SOURCE-REBUILD.zh-CN.md`。安装官方 ESP-IDF v5.4.2（包含 esp32s3 工具链）及其 Python 环境，然后在本源码目录运行：

```sh
. /path/to/esp-idf-v5.4.2/export.sh
idf.py -B build-relinked build
```

发布包已经包含 `components/` 和 `managed_components/` 的源码，且 M5Unified 有本地音频修补。不要运行 `fetch_repos.py`：归档没有依赖的 Git 元数据，它会 `git fetch` 失败；也不要用上游原版覆盖本地修补。`repos.json` 记录原厂五个依赖的基线标签，`dependencies.lock` 固定组件管理器
解析结果。`build-relinked/cardputer-adv.bin` 是原厂 app，地址 `0x340000`。保留原厂
13 个应用及其代码；不承诺在没有对应配件的情况下验证 LoRa、GPS、红外等
外部硬件功能。

共享存储主机测试：

```sh
c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Itests/fakes -Imain/ekko_shared \
  main/ekko_shared/SharedWifiStore.cpp tests/test_shared_wifi_store.cpp \
  -o /tmp/m5-factory-wifi-store-test
/tmp/m5-factory-wifi-store-test
```

## 本地只读诊断

在 USB 串口发送以下任意一行（LF 结尾）：

- `FACTORY_HEALTH`：返回同名行前缀和 JSON，包含固件、分区、当前菜单、连接
  状态、共享配置是否有效和原因码，不返回 SSID、密码或认证内容。
  `wifi_associated` 表示 AP 关联；`wifi_connected` 需同时满足关联、HAL 获 IP 和接口非零 IP；`wifi_state` 区分 off/disconnected/connecting/getting_ip/connected。`wifi_last_disconnect_reason` 为数字断线原因，`wifi_disconnect_count` 为累计次数。
  `uptime_ms` / `reset_reason` 用于区分自然掉线与复位；`stacks` 报告 main/event 实际分配字节和任务运行期间最低剩余字节，event 任务不存在时无其最低余量，不能按零解释。
  `boot_wifi` 报告一次开机尝试状态：not_started/no_saved_network/connecting/connected/failed，不代表之后永远在线。
  `menu_items` / `menu_names` 来自实际注册应用，`last_screen` 包含最近截图的
  状态、已进入发送队列的像素字节数、耗时与 CRC32。
- `FACTORY_SCREEN`：返回 `FACTORY_SCREEN 240 135 64800 RGB565LE` 一行，紧接
  64,800 字节。每像素为小端 RGB565，由原厂三个已绘制 sprite 合成，代表
  提交给 LCD 的画面来源，不是物理 LCD 面板读回。
  完整发送后附加 `\nFACTORY_SCREEN_END 64800 <8位CRC32十六进制>\n`，CRC32 可与
  主机 `zlib.crc32(payload)` 比较。

不提供键盘注入、任意内存读取、写 flash 或配置命令。串口截图期间尝试获取
stdout/stderr 锁，忙时直接放弃；发送用原生 USB Serial JTAG ISR 驱动，每块
最多 64 字节，完整检查短写/零写/排空结果，不经过会静默丢字节的 VFS TX。
4 秒绝对截止时间覆盖整帧与排空，失败后恢复主循环，并在下次健康状态中报告，
不会把错误文字插入半截像素数据。主机应以长度、尾行和 CRC32 三者确认完整，
连接中断后的半截帧不能当作成功截图。

诊断边界补充：FACTORY_SCREEN返回内存sprite快照，动画后缓冲可能被清空，不能据其黑帧判断面板黑屏，也不能把它当作当前LCD菜单画面的证据。CRC只验证传输完整性；菜单实体操作以用户实物确认、注册清单以运行时遍历为准。

配网流程主机测试：`c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain tests/test_wifi_setup_model.cpp -o /tmp/cardputer-wifi-setup-test && /tmp/cardputer-wifi-setup-test`。

## 时间显示

首页状态栏和 Clock 使用北京时间（UTC+8）。启动时设置 POSIX TZ 为 `CST-8`，网络校时仍保存 UTC 时间戳，显示时统一经 `localtime_r` 转换。首页不再标 UTC；Clock 标注「北京时间」。重启后会先尝试已保存网络并进行 SNTP 校时；如显示 `up` / `System Uptime`，表示还未校时，可在 SetWiFi 连网后等待。启动计时不会冒充当前时刻。

## 串口观测与复位干扰

原先在打开 pyserial 前设置 DTR=False、RTS=False，会在这台原生 USB Serial/JTAG 设备上触发 `USB_UART_CHIP_RESET`，查询后离线不能直接认作自然断网。采用官方工具使用的安全控制线次序，先 True/True 后打开，再依次释放 RTS、DTR。保持同一会话完成连接测试，避免不断打开/关闭串口。

```python
import serial
import termios

port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
port.dtr = True
port.rts = True
port.port = "/dev/cu.usbmodem3101"  # 替换为当前设备端口
port.open()
port.rts = False
port.dtr = False
attrs = termios.tcgetattr(port.fileno())
attrs[2] &= ~termios.HUPCL
termios.tcsetattr(port.fileno(), termios.TCSANOW, attrs)
try:
    port.write(b"FACTORY_HEALTH\n")
    # 按行读取；测试期间保持端口打开，结束后再关闭。
finally:
    port.close()
```

关闭 HUPCL 是为减少关闭端口时的控制线干扰。本机本次已实测安全 close/reopen：uptime345025→348803ms，连接和复位原因保持，不代表所有系统、驱动或适配器都绝不会复位。`FACTORY_HEALTH` 字段与持续日志应结合 uptime 和 reset reason 判断。

系统事件栈与 main 栈配置均为8192字节，当前 IDF 各有512字节余量，实际8704字节。事件栈单独扩容中间版的实测峰值3096超过旧2816字节；main 旧分配4608只余168字节，所以最终候选同时补足两处。最终实机 main/event 最低余量分别4152/5608字节；无需再次输入即自动连接保存网络，14条健康记录覆盖123.257秒、零断线且uptime单调。该测试是刷写后设备启动与主机读取，不冒称物理断电或用户最终按键验收；完整因果证据与边界见交付报告。
