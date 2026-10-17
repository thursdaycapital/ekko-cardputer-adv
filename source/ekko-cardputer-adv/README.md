# Ekko Studio 原生 Cardputer ADV

## 当前版本 v8.2.4.1 · Fusion Pixel 原生12px

构建标识`cardputer-adv-v8.2.4.1-fusion12`。应用3,185,392B，SHA256 `1840130458d4329a055f5981b76ec9805958e0f6191ccccfaacd634cf4923e4f`。菜单、Agent名、正文、输入框、候选和状态统一12px，正文行距14px；顶部显示「中文 Ctrl+Space」／「英文 Ctrl+Space」。Fusion原始字形加180枚efont12兼容字，24,025字，完整覆盖旧拼音候选。

本应用已经过实际烧录、完整闪存读回，并由用户确认「显示正常，字号和提示符合要求」。它使用当前配套的电脑端MCU扩展协议，要求Agent ACK与`chatProtocol:1`；并非任意未适配的官方Studio版本即装即用。首刷与保留设置升级请阅读安装包根目录教程。此源码目录不负责写设备启动器或原厂分区。

以下v8.2.4及更早章节为历史记录。


## v8.2.4 清晰中文与状态提示（历史）

构建标识 `cardputer-adv-v8.2.4-cleartext`。应用3,002,064B，SHA256 `3069b54d79d940605ef655c9d985b098b5f21cc2bc0e6134f9268bf8d7be3dc5`；仍仅更新既有app0@0x10000，原厂菜单/分区不变。

聊天正文、输入、提示和Agent首页中文统一使用官方原生14px点阵，行高16；聊天顶部独立状态区为官方16px白字（3,537B字形子集），保留Tab历史、中英提示、Fn＋空格、发送及拼音候选。动态正文使用完整14px字库，状态子集不限制聊天内容。去掉重复12px字库后app分区剩余340,272B。字形比较来自原始压缩字体；不使用插值放大或替代字体。

状态策略区分真实Wi-Fi断开、需登录、自动验证、后台连接、Agent确认和为远程语音临时释放Socket；listening正确显示正在录音，匹配ACK前不误报需更新。未修改官方中继、绑定、语音编码或按键声。逻辑/排版回归、构建及烧写读回通过。用户已在M5实屏确认「字清楚了，页面正常」；设备日志也完成真实消息/远程音频播放及恢复Agent，听感不据日志代替。

本节及后续较早章节为历史记录；当前镜像和字体以本文件最上方 v8.2.4.1 说明为准。

## v8.2.3 自动远程恢复（历史）

构建标识 `cardputer-adv-v8.2.3-autoreconnect`，应用 2,949,088B，SHA256 `06be7f53b6fc440d493f369c2b33107a47374f618b242a349d07a878c51ed4ee`。原厂菜单项现名为 `EkkoStudio`；本页较早版本章节保留设计与验收历史，版本号不代表当前镜像。

复用官方中继、已绑定电脑与 default Profile，增加连接/Agent ACK期限、失效凭据恢复、Wi-Fi后台重试及音频队列鉴权隔离；无新桥接或云接口。四路TLS启用固定SDK原生CA包的证书链和主机名校验，无insecure回退，但预编译SDK仍关闭证书日期校验。

2026-10-16实机：手机热点换网后约18秒自动恢复；文字输入、Fn＋空格识别、文字回答和实际回复声音经用户确认。电脑中继重新开启后约14秒自动重新鉴权恢复。以上没有再次MCU网页登录或Profile选择。设备物理重启及手机热点关闭约3秒后恢复、收到回复均由用户确认。该短中断未捕获明确STA重关联，不推广为长断网或自动AP恢复已通过。测试网络 A首次约435秒，且有间歇TLS/HTTP/心跳超时，不满足三分钟稳定可用目标。

本轮完整实机记录、全部发现/置信度及回退/校验说明见本轮 `cardputer-cu-autoconnect-20261016/` 交付目录。以下历史功能章节不替代这份当前验收状态。

## v8.2.1 按键音输出连续性修正（历史）

v8.2 实听已确认有声，但用户反馈音色不同或有杂音／断音，语音仍正常。v8.2.1 修正每键重启 codec 的输出生命周期：空闲手动模式保持 UI 扬声器运行，首次取得输出时先异步播放全零样本稳定时钟，正常键音完成后继续复用输出。SDK channel 1 的精确源指针释放回调通过原子标志交还持久缓冲，之后才能重写；不再逐键 end／begin，也没有逐键 51 ms 的额外等待。

录音、VAD、TTS、静音、待机和返回原厂仍优先关闭并 join UI 输出；TTS 显式接管并恢复原有 volume 180。没有回退两版 SDK 的 S3 样本打包差异，避免重新引入语音镜像噪声。原厂输入 PCM 和音阶保留，最终声学效果仍需实听。

`audio.key_click` 新增 speaker_owned／warming／buffer_free／speaker_starts／warmups／buffer_releases。连续普通按键时，speaker_starts 应保持不变、played 和释放计数增加；completed 只表示源缓冲已消费，不表示实体尾音已经播放完。新版本 `cardputer-adv-v8.2.1-keyclick`。 用户已实机确认按键声「可以了 就这样」、语音「语音仍正常」，当前版本保留。

## v8.2 按键声（历史）

EkkoStudio 现在复用原厂短音阶按键声：物理键按下时响一次，数字 1–7 对应固定音阶，其余键随机选择同组音符；长按自动重复和松开不额外响。采样与播放解释保持原厂实际参数，主音量 90/255，沿用原厂 `cardputer/quiet_mode` 静音偏好；Ekko 输出音量设为 0 也不响。

提示只在主循环安全点开始播放，使用独立 channel 1 与持久的 3,840 字节样本缓冲。录音、自动监听、语音播放及待播队列优先，不积压后补按键声。源样本消费完后保留 DMA 排空尾音，再 join 扬声器任务；录音和 TTS 可立即抢占。TTS 继续使用原有音量 180 与 PCM 音量处理，不受提示音设置影响。

`/health` 的 `audio.key_click` 提供真实物理按下、丢弃、播放、完成、抢占和失败计数，仅含诊断元数据。新构建版本 `cardputer-adv-v8.2-keyclick`。本次更新仅写既有 `app0@0x10000`，不改原厂固件、分区表、NVS 或登录配置；原厂菜单仍名为 `EkkoStudio`。

## v8.1 双固件适配（历史）

v8.1新增双固件适配：原厂 CardputerADV UserDemo 菜单中的原 `Ekko V8`（现名 `EkkoStudio`）启动 ota_0，原厂位于 factory@0x340000。聊天 Esc 返回 Agent 页，再按 Esc 返回原厂；Ekko 启动后将下次启动目标恢复为原厂，所以断电重开先回原厂菜单。共享分区和两套固件必须成对安装，不能沿用旧分区表。

原厂 SetWiFi 成功后发布 `cardputer/ekko_wifi_v1` 单一 NVS blob，Ekko 优先复用。两套 SDK 的 Wi-Fi driver 使用 RAM 存储，SDK 私有 blob 不作为共享协议；旧原厂键与 Ekko 历史仅作迁移/连接回退。新增 `SharedWifiConfig.h`、`FactoryFirmware.cpp` 及默认 NVS 整区擦除保护；普通 NVS 页 GC 不受阻止。该历史构建版本 `cardputer-adv-v8.1-dual`，功能基于已实机验收的 v8。

独立 ESP32-S3 目标，从 `packages/esp32-c3/v2/src/main.cpp` 完整应用逻辑移植。
基线为 Ekko Studio `1242ac63556491c92ba6dd49d89fd19c91a6bdd5`。不修改 v1/v2 C3 固件。
此目标直接连接 Studio 原生 HTTP / Socket.IO，不使用 Buddy daemon、Python bridge 或旧串口协议。

保留原生 Wi-Fi 配网、5 组 Wi-Fi 历史、局域网/手动/远程机器发现、MCU 登录、profile
选择与持久化、JWT、Socket.IO `/global-agent`、退避重连和重新认证、IMA-ADPCM 语音流、
WAV/HTTP 兼容辅助代码、PCM/ADPCM 下行、字幕分页、工具/交互状态、停止/清空会话、音量、
本地 VAD 自动监听和连接待机。按键和 VAD 现在均走 ADPCM 流，保留的 WAV/HTTP 辅助函数没有自动后备调用。
C3 OTA 在此目标明确禁用，`/ota/check` 返回 409；
下载写入入口也返回失败，不会把 C3 镜像装到 S3。

## 构建

```sh
uvx --from platformio pio run -d packages/esp32-s3/cardputer-adv -e cardputer-adv
python3 -B packages/esp32-s3/cardputer-adv/tests/run_tests.py
python3 -B packages/esp32-s3/cardputer-adv/tests/run_render_tests.py
python3 -B packages/esp32-s3/cardputer-adv/tests/run_chat_tests.py
node packages/esp32-s3/cardputer-adv/assets/avatars/generate.mjs --check
python3 -B packages/esp32-s3/cardputer-adv/assets/avatars/test_assets.py
```

依赖和框架在 `platformio.ini` 固定；8 MiB Flash，无 PSRAM 假设。
应用镜像为 `.pio/build/cardputer-adv/firmware.bin`，应用偏移 `0x10000`。
NVS 保持 `0x9000/0x5000`，两个应用分区保持 `0x10000/0x330000`、
`0x340000/0x400000`。烧录由操作者完成，先保留设备私有备份。

## 使用

- Agent选择页：`, ←` 或 `; ↑` 选前一个；`/ →` 或 `. ↓` 选后一个，**无需 Fn 或回车**。Shift、Ctrl和大小写锁定不影响选择识别；按住不连切，快速连按合并到最后目标。
- 后台 `mcu.agent.selected` 确认后才改变实际路由；等待确认、连接中断或确认失败时禁止新录音。选择状态和中文确认错误优先于旧任务画面；切换确认后清掉旧任务显示。
- Agent页OK/Enter进入当前Agent的对话页。等待原生ACK时先显示正在打开；进入只加载该Agent/profile的原生历史，不启动录音或模型。
- 对话页普通字母、标点和空格输入，OK/Enter发送；Shift+Enter或Fn+Enter换行，退格删除。Fn+左/右移动光标，Fn+[ / Fn+]移到本行首尾，Fn+退格向后删除。草稿512字节，按Agent/profile分别保留，断线可继续编辑。默认中文拼音模式：拼音连写、空格或数字1–5选字词，[ / ]翻候选页；Ctrl+空格切英文。组合时OK先上屏，再按OK才发送。英数符号和中文回复仍正常显示。
- **Tab进入历史浏览**：裸`; ↑`上翻、`. ↓`下翻，`, ←`上一页、`/ →`下一页，R回到最新；Tab或OK返回输入，草稿保留。输入页也保留Fn+上/下滚动、Fn+R刷新。遇到长回复窗口边界会加载前/后续内容；列表以12条双向分页，后台保存完整原文。正在生成时保留流式正文，完成后再刷新。
- Esc（左上角反引号键，无需Fn）返回Agent选择；Fn+Esc可输入反引号，配合Shift输入波浪线。返回不清空会话或草稿；未提交的录音会取消。已提交任务仍由原生后台执行；播放期间Esc会请求停止播放。
- 对话页 **Fn+空格或顶部G0** 长按说话、松开发送，单击停止、双击清空当前原生会话。普通空格用于输入或拼音选词，不触发录音。VAD也只在对话页启用。录音时打字会取消尚未提交的录音，再处理文本；播放期间可以编辑下一条草稿。
- 手机/电脑访问屏幕所示设备 IP 的 `/device`：原生机器发现、登录、profile、全部 16 项 Agent
  选择、音量、监听模式和待机设置。未配网时连接 `Ekko Studio-WIFI` 并打开 `192.168.4.1`。

本地内置 `config/agents.json` 中的全部 16 项稳定 ID、名字和顺序；测试会拒绝目录漂移。
它们是 Studio 的 Agent 类型，profile 是另一维。选择确认只证明原生路由已设定，
不证明该 Agent 的 CLI、提供商、模型或登录已配置成功。执行结果仍由原生后台返回。

选择握手扩展：`mcu.ready` 和 namespace auth 携带 canonical `agentId`，兼容字段
`agentRuntime` 使用同一值。`mcu.agent.select {agentId,profile}` 返回
`mcu.agent.selected {agentId,profile,selectionVersion}`；版本是整数且至少 1。
所有普通 JSON MCU 事件携带该上下文；原生二进制 chunk 继续通过 interactionId 绑定到带上下文的 stream，WAV 兼容上传辅助代码另带 `X-Hermes-Mcu-Agent-Id` 和
`X-Hermes-Mcu-Selection-Version`。带上下文的旧 Agent/旧版本状态与音频会被丢弃。
旧后台没有有效确认时显示未确认并阻止语音，不自动回退 Ekko。

v5要求后台ACK声明`chatProtocol:1`；缺失时明确提示更新电脑端。`mcu.chat.send/history`沿用原生认证与已确认的Agent/profile/version，后端自行派生设备所有的session，拒绝客户端指定sessionId。正文独立于TTS实时分片，HTTP429与TTS失败在页内提示。打字走正常文本指令，不套用语音简短回答提示。
发送只有收到accepted才清除同版本草稿；可同时编辑新内容。请求ID进程内去重，断线/超时不自动重发，结果未知时先Fn+R核对历史。后台重启后不保证旧ID去重，因此不能用自动重发恢复。

## 硬件与配置

`CardputerHardware.cpp` 使用锁定 M5Unified 的 ADV 板型识别、ES8311 codec 回调、
LCD、TCA8418 键盘、M5 Power。删除 C3 GPIO 直接初始化和 I2S driver 安装。
扬声器和麦克风切换拥有共享 codec 时钟；音频缓冲在异步读写完成前保持有效，
超时取消会等待任务结束。LCD 使用 240×135 RGB565 离屏画布，内部 DMA 堆分配
64,804 B；完整画好后只提交一次，不在物理屏幕上清屏重画。首页按可见状态去重，
忽略旧 OLED 眨眼缓冲；交互像素变化最短间隔 80 ms，导航/错误立即更新。
分配前检查最大连续 DMA 块和至少 96 KiB 剩余内部堆；不足时仅显示一次小错误条。
单次提交消除清屏中间态，但不保证硬件 VSYNC 或绝对无撕裂。
原生 U8g2 中文字幕逻辑仍在 RAM 绘制，再呈现于彩色 LCD。

16 个头像使用真实品牌图像：12 个 SVG、4 个 PNG。保留 Ekko 上游采用的原图，
并优先使用能查到的品牌方原始 SVG；不描摹、不包裹 PNG 为伪 SVG、不使用字母占位。
源图在 `assets/avatars/originals`；来源层级、URL、SHA-256 与许可说明见
`assets/avatars/manifest.json`。它们在构建前转为 44×44 RGB565 PROGMEM，设备无须
联网下载或运行 SVG 解析器。原图更新后运行 `node assets/avatars/generate.mjs`
（从本固件目录执行），再构建。品牌图不因被 Ekko 收录就获得额外商标许可。

首次 `net/ssid` 为空且 `net/migrated` 未设置时，从旧设备的 `ekko-wifi/ssid,password`
迁移到 `net/ssid,pass`，保留 UTF-8，不打印密码。迁移完成后标记 `migrated=true`。
`/clear` 和清空历史会保留该标记，重启不会恢复旧网络。原生 MCU 状态继续保存在
`mcu` namespace，新选择保存为 `agent_id`，启动时的原生清理流程也保留它。
公共代码和构建产物不含用户凭证；私有 NVS 不属于公开固件。

## 只读验收

`GET /health` 增加 `hardware/firmware/agentId/agentLabel/agentCount/browseAgentId`、
`selectionPending/nativeAgentConfirmed/selectionVersion/physicalNavigationCount/selectionError`、
`navigation.left/right/up/down/queued/dropped`（四个方向的实际按下边沿和队列状态）、
`navigation.ok/back` 和 `ui.page/open_pending`（真实 OK/返回计数与独立页面状态）、
`chat.protocol/ready/messages/draft_bytes/accepted/received_chunks/rejected_frames/key_count/key_dropped/cache_bytes`（只有计数，不含消息正文）、
`uptime_ms/free_heap/max_alloc_heap/loop_stack_min_free_bytes`，以及 `render` 下的实际 LCD 帧计数、跳过次数、
离屏缓冲大小、渲染耗时、分配失败、内部/DMA 空闲堆与最大连续 DMA 块。
`frame_count` 只在实际提交且 DMA 完成后递增。静止画面跨 10 秒应不增加。
这些字段不包含 token 或 Wi-Fi 密码。
`GET /screen.bmp` 尝试逐行读取真实 LCD 并输出 240×135 BMP；驱动声明不可读时返回
503 `lcd_readback_unavailable`，不会生成替代图。它是当前画面，可能包含语音字幕，
分享前先检查内容。实际读回是否正确需实机确认。

原生测试使用实际 AgentSelection/NativeEnvelope、固定 ArduinoJson 和 ASan/UBSan，
覆盖 16 项目录、循环选择、ACK 匹配、版本、断线、快速目标更新、旧任务与选择状态的显示优先级、空 JSON、嵌套键和旧事件。
编译与本机测试不代替键盘、录音、播放、VAD、字幕时间线、重连、待机和登录的实机验收。

| 已知边界 | 严重性 | 置信度 |
|---|---|---|
| 按键录音/识别/扬声器已在LAN及Hermes官方远程链路实机通过；自动VAD阈值未独立验收，16头像/四方向历史验收保留 | 中（VAD验收缺口） | 高 |
| 原生setup AP开放，设备管理HTTP沿用原生认证模型；四路远程TLS现做CA链/主机名校验，但预编译SDK关闭证书日期检查，错误证书拒绝尚未实机注入验收 | 中 | 高 |
| 原生应用有同步扫描、HTTP/TLS/音频循环，部分管理请求可能等待当前操作；左右键播放期间会请求停止、录音期间会取消流，再处理选择 | 中 | 高 |
| 上游原生 login profiles 最多 8 个，与 16 Agent 类型无关 | 低 | 高 |
| 无 PSRAM，离屏画布增加 64,804 B 内部 DMA 堆占用；保留但无自动调用的 WAV 辅助函数一次最多约 128 KiB；远程 TLS、字幕和网页并用峰值仍需验收 | 中 | 高 |
| 彩屏聊天、中文输入、历史翻页、按键声与回复声音已有用户实机确认；未覆盖所有音量/增益组合 | 低 | 高 |
| 首次库安装可能提示上游 IRremote 通配依赖歧义；直接依赖已明确锁定 z3t0/IRremote 4.7.1 | 低 | 高 |
| WS部分帧读取失败现会关闭transport，避免继续错位读取；FIN/continuation未重组、Engine.IO close包未显式处理，仍有协议边界，尚无本次碎片证据 | 中 | 高（源码边界，实际影响待验） |
| 原生远程HTTPS音频仍会临时断开Socket节约内存，恢复ACK后继续有效队列，鉴权变更清理旧媒体；Hermes实际声音已通过，长篇多分段/多Agent并发未穷举 | 中 | 高 |
| backend 选择版本为 JavaScript safe integer，固件使用 uint32；同一后台进程超过约 43 亿次切换后会拒绝确认 | 低 | 高（理论边界） |
| Mic timeout=0 只保证等待录制完成时不阻塞；首次 record 仍可能初始化或重配采样率 | 低 | 高 |
| Speaker isPlaying 清零证明源缓冲已消费，不代表声学尾部已播完；原生 silence/drain 流程需检查最后音节是否完整 | 低 | 中 |

| 部分继承网页标签仍写 ESP32-C3；实际构建、芯片、硬件状态和 /health 为 Cardputer ADV，协议 legacy URL 与持久设备标识保留兼容 | 低 | 高 |

Socket.IO 串口日志只记录事件类型/帧长度，不输出整帧、relay token、JWT 或字幕内容。

## v6 连续播放与历史浏览

音频输出使用3×2048字节固定PCM缓冲，以M5Unified的buffer-release回调回收，播放过程中只等待空闲源槽，不再每256帧等待全部播放结束。流末先等待源释放，再保留DMA/codec尾音时间；取消或超时必须Speaker.end()完成后重置缓冲，避免仍被SDK读取的指针复用。音量、codec增益与TTS格式保持原设置。

聊天页常驻Tab历史；按Tab后隐藏输入框扩大正文，裸上下键滚动、左右键双向翻12条、R回最近。Tab或OK返回输入，未发送草稿保留。`/health.chat`有history_mode、bottom_offset、max_bottom_offset及各实体动作计数；`/health.audio.stream`有排队/释放/超时计数。source_empty_before_submit只指源槽已空，不代表DMA声学断流，不能据计数代替听感。

## v7 中文输入与简洁composer

聊天页右侧语音标签已移除，只显示发送；空白输入框保留“Fn+空格说话”提示。Fn+空格及顶部G0仍为原生语音功能。中文模式可拼音输入和选字，Ctrl+空格切换英文；Tab进入历史再返回不会清掉草稿或未上屏拼音。拼音/候选仅保存于板端当前Agent/profile上下文，选词成功后以UTF8写入原生512字节草稿，容量不足时保留完整候选与拼音，不截成半个字词。候选库为离线Flash数据，不上传每个按键。

### v7.1 提示调整

按用户截图保留空白输入框中的“Fn+空格说话”，只移除右侧“语音”标签；普通输入页发送在右下，拼音组合时使用候选布局，提示不遮挡正文或候选。

### v7.2 消息正文阅读

用户气泡直接显示正文，不自动添加“我”，正文原本的“我”字仍保留。助手常见Markdown格式在屏幕转为阅读正文；代码/算式的星号按原文保留，用户消息与输入框不经过该转换。原生DB、网络字节offset及TTS原文保持不变，计行、滚动及可见文字共用显示副本。固定1025B临时显示缓存，不为历史保留额外完整副本。

默认呈现为表情的Unicode符号使用文字标签显示，例如[招手]、[微笑]，避免缺字方框；控制修饰符只在显示副本处理。源数据、Unicode版本、来源校验与许可在assets/chat-symbols。Markdown仅处理常见配对格式；从正文中段续读或超过固定解析预算时保守显示原文，参考链接、HTML、表格/LaTeX不做完整桌面渲染。不能将这一层当作完整Unicode字体或CommonMark实现。

### v7.3 无角色文字前缀

双方气泡均直接从正文开始，不自动画“我”或“AI”，首行不再为角色字预留18px。左右位置和背景仍区分双方；实际正文中的我/AI不删除，Markdown/表情显示副本与中文/语音/原生存储保持原功能。

## v8 开机短片（实机验收通过）

应用启动时先播放用户提供的完整4秒静音短片，然后继续载入原有Wi-Fi、原生账号/Profile与Agent首页。动画出现后可按任意键或G0跳过；当前按住的跳过键会先清掉按下边沿，避免直接进入聊天或触发语音。画面异常时退出动画并继续正常初始化；网络连接状态仍按真实结果显示。

设备使用离线JPEG帧，不需要SD卡或联网下载。素材表/偏移、帧数和容量由生成器校验，解码到已有RGB565画布；完整解码及LCD传输完成后才统计为呈现。GET /health 的boot_video单列attempted/completed/skipped、帧数、时长、解码耗时、慢帧及错误；render.frame_count包括实际启动帧，不能再假定与常规draw_calls一一对应。播放失败/跳过均不改Wi-Fi、登录或NVS配置。

播放器调度回归：`python3 -B packages/esp32-s3/cardputer-adv/tests/run_boot_video_tests.py`。主机回调/素材校验、真实设备解码计数和用户实际观看分别验收，不能相互替代。最终采用用户指定的原4秒视频，由Remotion4.0.535渲染并保留完整画面；MP4保原2K60仅146911B，板端60帧JPEG及偏移表225645B，均<300000B。v8已单应用刷入，实机完整60帧/4001ms/0解码失败后进入Agent首页，用户已确认正常。跳过和异常注入未另作实机验收，主机回归覆盖。
