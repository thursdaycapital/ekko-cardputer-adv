/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <hal/hal.h>
#include "WifiSetupModel.h"

class AppSetWiFi : public mooncake::AppAbility {
public:
    AppSetWiFi();
    ~AppSetWiFi();
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
private:
    WifiSetup::Model _model;
    int _key_event_slot_id = -1;
    uint32_t _opened_at = 0, _cursor_time = 0;
    bool _cursor = true;
    void render();
    void render_input();
    void text(int x, int y, const std::string& value, int width, uint32_t color, bool tail = false);
    void footer(const char* first, const char* second);
    void handle_key_event(const Keyboard::KeyEvent_t& event);
};
