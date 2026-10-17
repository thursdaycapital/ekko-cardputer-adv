#pragma once
#include <hal/hal.h>
#include <functional>
#include <string>
class ReplView {
public:
    inline static ReplView* active = nullptr;
    std::function<void()> onRenderTips;
    std::function<void(const std::string&)> onCommand;
    bool autoClearPrompt = true;
    std::string message;
    uint32_t color = 0;
    size_t updates = 0;
    ReplView() { active = this; }
    ~ReplView() { if (active == this) active = nullptr; }
    void init() { if (onRenderTips) onRenderTips(); }
    void update() { ++updates; }
    void showMessage(const std::string& value, uint32_t value_color) { message = value; color = value_color; }
};
