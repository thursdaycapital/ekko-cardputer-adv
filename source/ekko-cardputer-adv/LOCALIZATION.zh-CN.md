# Ekko Studio Cardputer ADV 中文界面

固件版本：`cardputer-adv-v8.3.0-zh-fusion`。

基于已验收 `cardputer-adv-v8.2.4.4-audio-buffer`。本次仅调整显示文案、显示字体调用和 UTF-8 文案截断；不调整语音录制、编码、接收、播放或缓冲算法。

## 覆盖范围

| 界面 | 覆盖 |
| --- | --- |
| 设备 LCD | 原生连接/待连接顶栏、启动/网络/登录/录音/播放状态、显示内存/状态异常、智能体选择错误 |
| 对话 | 保留原有中文状态、中文 Ctrl+Space、Fn+空格说话、发送、历史、候选；补全清空会话状态；原始英文错误改为中文提示 |
| 配网网页 | 网络名称、手动网络名称、连接/失败/共享配置保存提示、正确的 Cardputer ADV 硬件标识 |
| 设备网页 | 设备、音频、语音模式、人声检测、智能体、网络、固件更新、手动设备错误 |
| 登录与配置档网页 | 设备登录、配置档列表/选择/切换/失败；`default` 显示“默认配置” |
| 诊断 | 音频、授权、网络、限流、缺少语音识别配置与未知错误均提供中文提示；原始错误保留在诊断状态/串口 |

品牌、16 个智能体原名、键帽名称、地址、机器 ID、配置档自定义名称和真实聊天内容保留。协议字段、路径、运行时 ID、保存值与 HTML option value 均保持原值，`default` 仍以 `default` 提交。

LCD、聊天与旧单色 RAM 文本路径共用原有 `EkkoPixelFont12` Fusion Pixel 兼容字库，原生 12px 字号、基线及布局保持。该字库既有的 180 字 efont 兼容补字未更改。

原生网页通过本地 `GET /font/fusion-pixel.woff2` 加载官方 Fusion Pixel 衍生的 `Cardputer Pixel Web` 子集，CSS 的正文、按钮、输入框、选择框和信息字段均以该字体为首选，保留原有适合网页的字号。WOFF2 使用 `font/woff2` 和显式 105692 字节长度发送；无需外部 CDN，动态罕见字符可使用浏览器中文后备字库。

## 验证

- PlatformIO `cardputer-adv` 独立完整构建与增量构建成功。最终包含 Webfont：RAM 116264/327680，Flash 3300169/3342336（98.7%）。实际 `firmware.bin` 为 3300528 字节（`0x325cb0`），小于 app0 的 `0x330000`，余 41808 字节。
- 最终应用镜像 SHA256：`c5898d0e79d600aec884c5be7c2e08a15a3301bc6a983b19ed5cd389546e7448`。
- `run_localization_tests.py`：379 个非 ASCII 界面字符同时被现有 LCD Fusion 兼容字库及网页 Fusion 子集覆盖；53 个静态状态参数均有翻译。配置档显示与真实值分离、实际显示边界接入、UTF-8 中英混排截断及 0/1 边界通过。
- WOFF2 文件、C++ 嵌入数组及二进制中单一完整副本字节相同，大小与 provenance SHA256 一致：`55818900b983245627daf6ccdad8eee9a0988196b540b8d7d496654993ee8936`。
- `run_tests.py`、`run_render_tests.py`、`run_chat_tests.py`、`run_shared_wifi_tests.py` 全部通过；包括真实 16 个头像、65536 个 RGB565 值、12px 字号/基线/裁剪、原生路由、导航、输入和重登录。
- `run_chinese_input_tests.py`、`run_chat_symbols_tests.py`、`run_plain_text_tests.py`、`run_pinyin_dictionary_tests.py`、`run_boot_video_tests.py` 全部通过；包括 38072 条候选读音、25897 个源键组、65536 个 BMP 字形 oracle、1112064 个 Unicode 标量与 5138 个纯文本检查。
- `run_adpcm_receive_tests.py`、`run_audio_queue_tests.py`、`run_audio_byte_ring_tests.py` 全部通过。生产接收/播放提取 SHA256：`1ace935516d7d9a974191110718428e71e04b0095fdebc6a5b5b72513152ad03`。
- 与权威源比较，`main.cpp` 51 个和 `CardputerHardware.cpp` 10 个音频相关函数正文相同；音频头文件、导航、中文输入、16 智能体目录、Fusion 字体数据与原厂返回源码字节相同。缺少语音识别配置的字幕仅将 `profile` 改为“配置档”。

## 问题与边界记录

| 严重性 | 置信度 | 发现与处理 |
| --- | --- | --- |
| P2 | 高 | LCD 实际渲染顶栏及异常回退仍含英文，已汉化。 |
| P2 | 高 | 网页中 MCU LOGIN、MANUAL DEVICE、Profile、default、PUSH TO TALK、LOCAL VAD、bytes、SetWiFi 等残留英文，已处理显示层。 |
| P2 | 高 | 网页误标 ESP32-C3，已修正为 Cardputer ADV；固件更新页仍明确区分不兼容 C3 镜像。 |
| P2 | 高 | 登录、手动机器、音频及聊天错误直出原始英文，已在显示边界分类成中文；内部原始值保留。 |
| P2 | 高 | 原 `fitOledText` 按字节截断会破坏中文 UTF-8；已按字符边界修复并测试。 |
| P3 | 高 | 清空会话原先在实际对话状态栏显示“正在回复”；已增加“清空会话”。 |
| P3 | 高 | 旧单色缓冲区使用硬编码 5x7 字形；已共用 Fusion。该缓冲区不是原生 LCD 对话的最终渲染路径。 |
| P3 | 高 | `include/ChatStatusFont.h` 保留未引用的旧 efont16 资源；生产源码无 include/调用，不参与界面渲染。未删除未使用资源。 |
| P3 | 高 | 既有 219px 单行通知会裁剪较长中文，未扩大本次到已验收布局重设计。 |
| P3 | 高 | 加入本地网页字库后 app0 余 41808 字节，后续增长应继续检查真实 bin 不超过 `0x330000`。 |
| P3 | 中 | 自定义机器/配置档名称及真实聊天可能包含字库范围外的罕见 Unicode；本次固定界面缺字为零，不声称所有 Unicode 字形可用，网页可回退浏览器字库。 |

未刷入设备。主机渲染测试为 canvas spy 与源码/字形 oracle；不能替代 LCD 实拍、真实 Wi-Fi 登录或实际听音验收。
