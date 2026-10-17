# 最终组合字体资产：原生 12px

正式固件仅使用本目录对应的 **Ekko Pixel12 Chinese Compatible**。旧217字菜单子集、纯Fusion中文界面版与完整BMP评估版只留在work，不属于此次发布字体。

最终24,025字＝原Fusion中文界面23,845字＋原M5GFX efontCN_12补字180字。压缩数据654,185字节，SHA256为 `6b6ecb63da33f40e18c8839a9a9909f823886f1baa40a736f19e178a8dba9eac`。未按中文使用频率裁字；仅排除 `provenance.json` 明确列出的五段韩文字母/音节范围，保留其它原Fusion BMP字形。非BMP原字形1428个因U8g2的uint16接口不能编码，没有假称已覆盖。

原完整efont12可显示的7545个码点全部保留；实际38072条拼音候选涉及6749个汉字全部覆盖；67条固定菜单/状态/输入/ChatSymbols标签和新增「中文 Ctrl+Space」全部覆盖。纯Fusion原版缺145个现有候选汉字的问题已通过180字原生12px补字解决，未删词。上游自身未提供所有Unicode汉字，本组合版也不能宣称所有Unicode字符均可显示；具体缺字范围在 `evidence/unicode-coverage.json`。

## 固件接口

应用源中的 `include/EkkoPixelFont12.h` / `src/EkkoPixelFont12.cpp` 提供 `ekko_pixel_font::{kPixels=12,kBaseline=10,kDataSize=654185,kGlyphCount=24025,kData,kFont}`。`kData`和`kFont`各只有一个外部定义，多翻译单元共享。离线选字表不进固件。M5GFX和原U8g2字幕消费者使用同一份字节。

原字体保留了两个22px高日文特殊字和四个30px宽私用字。布局采用12px行高/基线10；M5GFX绘制时临时恢复原全字体raster metrics（height22/y_offset-15），并与调用者12px cell和原clip相交，绘制后恢复clip。这是显示裁剪，不是重画或缩放字形。原U8g2字幕应显式设置baseline位置后在top+10绘制，不能采用全字体默认高度22。

原M5GFX方法在直接使用12px布局metric绘制U+3031时会产生高度65537的矩形；通用raster metric方案已经全24025字主机验证，最大请求高度22，12px cell外无像素。父任务负责最终固件clip覆写与整页/实机验收。

## 标准重现

从本目录执行：

```sh
make -C upstream/u8g2-bdfconv-2.36.18 CFLAGS='-O3 -Wall -Werror'
upstream/u8g2-bdfconv-2.36.18/bdfconv -b 0 -f 1 -v -m '0-65535' EkkoPixel12Compatible.bdf -n ekko_fusion_pixel12_compatible_data -o rebuilt-font.c
```

`rebuilt-font.c` 是官方转换器的标准输出，大字库受 `U8G2_USE_LARGE_FONTS` 宏保护。要比较原始数组SHA，可用普通C编译器定义该宏和空的 `U8G2_FONT_SECTION(x)`，编译后以fwrite导出数组全部654185字节。与前述SHA核对即可；不需要自制字体编码器。

组合BDF保留每个原始字形的bitmap与advance。原Fusion完整上游BDF及官方下载ZIP仅保存在work，公开精确URL/SHA在provenance中；发布无需把完整原字库再链接进固件。efont补字BDF和对应原始解码记录一并保留，能独立重现本次组合数据。

## 验证范围与发现

- 实际编译最终外部CPP数组与header，取原M5GFX 0.2.33实际字体方法编译到内存画布；24025字、24601600像素与组合BDF/原efont像素逐一相同，advance一致，ASan/UBSan通过。
- 通用raster metric＋12px cell裁剪检查24025字、9225600像素全部一致，无异常大矩形，cell外像素为0。
- 上述是主机字形和度量证据，不冒充M5实机或整页测试。

| 发现 | 严重性 | 置信度 | 状态 |
|---|---|---|---|
| 原Fusion缺145个已支持拼音汉字，151条候选受影响 | P1 | 100% | 已补齐全部旧efont覆盖所需180字，未删词 |
| 全BMP944121B大于当前预算 | P2 | 100% | 保留评估证据，最终只排除五段明确韩文块，未裁中文频率 |
| 原Fusion全局metrics28×22会错误改变12px排版 | P2 | 100% | 独立layout与raster metrics，父任务集成 |
| 原M5GFX decoder负行号产生65537高矩形 | P2 | 100% | 通用原raster metric＋12px clip已主机验证 |
| 原两个22px日文字形超出12px行 | P2 | 100% | 保留原数据，显示按行裁剪 |
| 原字体自身未覆盖全部Unicode中文/非BMP | P2 | 100% | 明确列出覆盖，不伪称通用Unicode完整 |
| 12px比原14/16px小，实际观感尚需屏幕验收 | P2 | 100% | 字形未经缩放；实机验收由父任务完成 |
| 官方转换器Makefile的-O4不兼容当前Clang -Werror | P3 | 100% | 未改源码，make参数使用等效-O3 |
| 转换器链接时common对齐由0x8000降至0x4000 | P3 | 100% | 转换成功，实际运行decoder与像素验证通过 |

完整字体许可见NOTICE、OFL、EFONT-LICENSE和LICENSES，分发时必须保留。
