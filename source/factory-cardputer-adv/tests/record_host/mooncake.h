#pragma once
#include <string>
namespace mooncake {
struct AppInfo { std::string name; void* userData = nullptr; };
class AppAbility {
    AppInfo info;
public:
    bool closed = false;
    virtual ~AppAbility() = default;
    virtual void onOpen() {}
    virtual void onRunning() {}
    virtual void onClose() {}
    AppInfo& setAppInfo() { return info; }
    const AppInfo& getAppInfo() const { return info; }
    void close() { closed = true; onClose(); }
};
}
