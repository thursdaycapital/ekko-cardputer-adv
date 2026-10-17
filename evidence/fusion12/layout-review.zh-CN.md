# 全界面 Fusion Pixel 原生 12px：布局只读复核

2026-10-17 更新。**本稿取代上一稿“菜单12、正文14、状态16”的建议**。用户最新确认正文、输入框、顶部状态全部统一 Fusion 原生12px，并在顶部展示「中文 Ctrl+Space」切换提示。根任务从稳定 cleartext 版本制作字体专用副本；本审查未修改任何 source、生产服务、网络或 USB，不复查 stock 协议。

## 本轮接线与坐标

使用一份 extern Flash 字体数据。Fusion 的真实字形和 advance 不缩放、不重采样；字体对象默认度量应明确返回 height=12、y_advance=12、baseline=10、y_offset=-10，而非由压缩数据的全局最大字形推断。完整非 Hangul BMP 字形覆盖由字体产物清单确认，不能宣传成完整 Unicode。

| 区域 | 本轮坐标与范围 | 复核 |
|---|---|---|
| 聊天 Agent 名 | `(32,2)`、宽124 | 最长 DeepSeek Harness=96px，末端128 <156 |
| 聊天状态 | `(164,2)`、宽68、白字 | 4个汉字48px；两字“离线”24px；y+12=14 |
| 历史按钮 | `(32,18)`、宽65 | Tab历史42px、浏览历史48px |
| 输入模式/快捷键 | `(100,18)`、宽132 | 「中文 Ctrl+Space」或「英文 Ctrl+Space」均90px，末端190 <232；与上方状态不同一行，不冲突 |
| header /正文起点 | header33，普通正文34，有Notice正文52 | 原位置保持，分隔线y32 |
| 正文 | Fusion12、rowHeight14、消息宽194 | 四个常规行 y34/48/62/76，行内字高12；气泡按14行高 |
| Notice | 原 `(10,35)`、宽219，16px高条 | 字高12，仍保留完整多语字库，不改错误内容 |
| 非 composing composer | top98，草稿 y101/115 | 第二行应从117改为115，以14行高统一，cursor高度11；占位第二行也建议115 |
| composing composer | top84，草稿y87、预编辑y103、候选y119 | 候选y+12=131 <=133；原分隔线118可保留 |
| 发送按钮 | 原35×17框，文字 `(203,sendTop+2)` | “发送”24px；居中，y+12 <=框底；sendTop原102/116 |
| 历史底栏 | 原top117；文字 `(7,120)`、宽226 | 原完整按键说明174px，y+12=132 |
| 主页 Agent 名/选择状态/计数 | 原 `(63,37)` / `(63,61)` / `(63,80)` | 最长名96px；最长固定状态120px，均小于167 |
| 主页品牌/连接标签/地址/底栏 | 统一Fusion12，地址仍按像素UTF8裁切 | EKKO STUDIO66px、NATIVE / ONLINE90px、底部方向提示150px，均可放入原区域 |

顶部模式提示建议一直显示当前模式及 Ctrl+Space（包括拼音 composing 时仍显示「中文 Ctrl+Space」），以满足用户需要。原来 composing 状态下的“空格选字”提示若被替换，不应删掉候选区本身或改变空格选词行为。ChatStatus 的联网/鉴权语义、状态文案和白色前景保持原有逻辑；本次只改字体和坐标。

## 可见行数与不能机械修改的测试

原坐标不动、rowHeight由16改14后，`nativeChatVisibleLines` 的数学结果为：

| history / Notice / composing | 12px结果 | 对比稳定旧版 |
|---|---:|---|
| 普通 / 无 / 否 | 4 | 不变 |
| 普通 / 有 / 否 | 3 | **原2，改3** |
| 普通 / 无 / 是 | 3 | 不变 |
| 普通 / 有 / 是 | 2 | 不变 |
| 历史 / 无 | 5 | 不变 |
| 历史 / 有 | 4 | 不变 |

- `test_chat_renderer.cpp` 的字体断言应按新实际字体对象改为12；原“顶部状态16、其余14”不再适用。其坐标断言仅针对确实改动的header/草稿/按钮调整，候选119和composer98/84保持。
- `nativeTypographyAndStatusRegions()` 上表只有普通+Notice这一项2→3；不要把所有 visibleLines 都增加或把读取记录/历史状态改成别的语义。
- `test_chat_layout.cpp` 测的是参数化 UTF-8 wrapping/CRLF/96字节缓冲/viewport 算法，测试 measure 本来就是 ASCII6、中文12。其24px宽应容纳2汉字、96字节上限、firstIndent和源码 byte offsets **不应因字体切换更改**；`nativeChatViewport(14,5,...)` 的14是示例总行数，与字体14px毫无关系。
- `test_chat_browser.cpp` 上下滚动3行、先后页阅读边界、R刷新不发送、草稿留存等行为不应随字号改动；长文本恰好换行后的具体 maxBottomOffset 可按真实度量复核，不能为了让旧断言过而修改滚动步长。
- ChatModel/协议/UTF8/ChineseInput 的字节容量、分块、路由、会话和输入行为不应改。原候选容器最大48字节，小于renderer label可用61字节，当前此处不会截断合法多字节候选。
- `cardputerChatContentLines()` 和 `cardputerChatMessageScroll()` **必须改用与renderer完全相同的Fusion字体对象**。只改renderer会使页数、跳到消息、offset锚点和画面不同步。不能保留efont14，也不能用仅平均6/12的另一个measurement代替实际字宽。

## 全字库高度异常：实际源数据与处理建议

直接检查上游2026.09.01 BDF：常用字符cell为12、baseline10；但有6个特殊BMP原字形：

- U+3031「〱」：advance12、bbox `(6,22,2,-7)`。
- U+3032「〲」：advance12、bbox `(9,22,2,-7)`。
- 私用区 U+E100/U+E101/U+E110/U+E111：advance30、bbox `(27,11,1,-2)`。

上游字形保留意味着字库压缩数据 header 的最大高度可能为22、最大宽30。这是字体全局bbox，不是用户正文必须22px的理由。M5GFX `U8g2font::getDefaultMetric` 默认直接采用 `max_char_height()`、baseline=height+y_offset；若只换数据不覆写，会让字号/基线/host spy都误判22。应在字体子类的 `getDefaultMetric()` 中覆写实际排版12/10，`updateFontMetric` 继续读取原glyph实际宽和advance；只修改header[10]来迎合测试会掩盖真实glyph几何。

**覆写默认度量不等于裁切特殊字。** 保留baseline10时，U+3031/3032实际像素会画到一行起点 y-5 到y+16；rowHeight14无法自然容纳，会盖上一行或下一行。建议从同一字体对象的 `drawChar` 包装层统一限制每字的垂直绘图区到当前名义12px行框：与调用方原clip取交集，调用原字形绘制后恢复clip。这保留源字形数据且不改换行/advance。若根任务采用renderer逐行clip，也必须覆盖正文、输入、候选、notice和所有drawClipped路径，而不是只在正文加一个总content clip；总区域clip仍允许越行。裁切后的这两枚罕见竖排符号会显示截取部分，应在字体说明记录，不能宣称所有特殊字形像素都完整显示。

宽30的4枚私用字本身低于12px高度，不需要剪成12px宽；应使用真实advance30参加换行。当前body194、候选43、动态drawClipped等范围可容纳一字30。若用host spy无条件“非ASCII=12”来验证，它会错误放宽这些字的边界。

## 全部发现与验收边界

| 发现 | 估计严重性 | 置信度 | 本轮处理/判断 |
|---|---|---|---|
| 全字库默认最大字高22会使布局/基线不再是原生12 | P2 | 100% | 字体metric adapter显式12/10；真实M5GFX fontHeight应读到12 |
| 度量覆写后U+3031/3032仍可能跨行覆盖 | P2 | 100% | 原glyph=22高；须名义行框clip并记录裁切，不以全局content clip代替 |
| Hardware仍用efont14测量时将和Fusion12显示分裂 | P2 | 100% | 两个独立滚动测量入口必须同字体对象 |
| 普通Notice可见行数2→3，盲改所有测试会掩盖composer/footer越界 | P2（验证错误风险） | 100% | 使用上表，实际每区按原坐标重新算 |
| host canvas spy以data[10]读取字高、默认CJK12估宽无法验证metric adapter或特殊字宽30 | P2（验证缺口） | 100% | spy应读取对象的实际metrics；另用真实glyph解码/实际M5GFX验证覆盖与宽度 |
| 全字库在头文件constexpr会因多TU复制而暴涨固件 | P2 | 100% | 数据单份extern/cpp，最终ELF确认一份；适配header不要含大数组定义 |
| 非Hangul BMP字集不覆盖任意Unicode；未知字/韩文/增补字符需已有一致替代行为 | P2（覆盖边界） | 100% | 以最终codepoints/normalization为准；不能许诺任意语言均完整 |
| 原主页动态地址没有宽度裁切，统一12后仍不能只用drawString应付任意长值 | P3 | 100% | 完整字库避免中文缺字，UTF8按像素宽裁切，数据本身不改 |
| drawClipped原机制不加省略号，长地址/提示可能只显示前缀 | P3 | 100% | 明确视觉裁切；不是数据丢失 |
| 统一12较用户刚验收的14/16更小，物理清晰度不能靠host图证明 | P3（体验待验） | 100% | 本轮符合新字号选择；待实际屏幕确认，无编译/模拟冒充实机 |

## 刷新路径

继续保留 `waitDMA -> 同一240×135×16 sprite内完整绘制 -> 一次pushSprite -> waitDMA -> RenderPolicy.commit`。编译期字体固定，不需要给每个字符增加LCD更新，也不需要关闭同态跳过/80ms动态节流。glyph clip在RAM sprite上实施，不改变提交次数。为看字体而切换16 Agent/发消息，应依照原有状态变化触发整帧，不恢复老式clear-and-paint。
