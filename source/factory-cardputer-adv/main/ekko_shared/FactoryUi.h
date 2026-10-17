#pragma once
#include "EkkoPixelFont12.h"
#include <string>

namespace factory_ui {
struct AppLabel { const char* id; const char* title; };
constexpr AppLabel kAppLabels[] = {
    {"EkkoStudio", "EkkoStudio"}, {"Scan", "无线扫描"},
    {"Record", "录音回放"}, {"Chat", "设备聊天"},
    {"Remote", "红外遥控"}, {"REPL", "脚本终端"},
    {"SetWiFi", "网络设置"}, {"Clock", "时钟"},
    {"Keyboard", "键盘控制"}, {"IMU", "运动传感器"},
    {"SDCard", "存储卡"}, {"StringIR", "红外工具"},
    {"LoRaChat", "LoRa电台"}, {"GPS", "GPS定位"},
};
inline const char* appTitle(const std::string& id) {
    for (const auto& label : kAppLabels) if (id == label.id) return label.title;
    return id.c_str();
}
inline void removeLastCodepoint(std::string& text) {
    if (text.empty()) return;
    size_t last = text.size() - 1;
    while (last && (static_cast<unsigned char>(text[last]) & 0xc0) == 0x80) --last;
    text.resize(last);
}
inline std::string fitText(lgfx::LGFXBase& canvas, const std::string& text, int width) {
    if (width <= 0) return {};
    if (canvas.textWidth(text.c_str()) <= width) return text;
    const char* suffix = "…";
    if (canvas.textWidth(suffix) > width) return {};
    std::string fitted = text;
    do { removeLastCodepoint(fitted); }
    while (!fitted.empty() && canvas.textWidth((fitted + suffix).c_str()) > width);
    return fitted + suffix;
}
} // namespace factory_ui
