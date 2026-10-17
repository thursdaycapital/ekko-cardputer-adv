#pragma once
#include <mooncake.h>
#include <cstdint>

// A normal Mooncake menu entry. The two applications remain independent images.
class AppEkko : public mooncake::AppAbility {
public:
    AppEkko();
    ~AppEkko();
    void onOpen() override;
    void onRunning() override;

private:
    uint32_t _opened_at = 0;
    bool _attempted = false;
    void launch();
    void show_status(const char* message, bool error = false);
};
