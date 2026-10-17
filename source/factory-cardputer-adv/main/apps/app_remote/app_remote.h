/* SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT */
#pragma once
#include <apps/app_repl/view/repl_view.h>
#include <mooncake.h>
#include <hal/hal.h>
#include "ac_protocol.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class AppRemote : public mooncake::AppAbility {
public:
    AppRemote();
    ~AppRemote();
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
private:
    enum class View : uint8_t { Main, More, Compatibility, Nec };
    std::unique_ptr<ReplView> _repl_view;
    View _view = View::Main;
    factory_ir::AcSettings _settings;
    uint8_t _parameter_focus = 0;
    uint8_t _more_selection = 0;
    factory_ir::AcProtocol _protocol = factory_ir::AcProtocol::Midea48;
    std::string _status;
    std::string _completion_status;
    std::string _pending_nec_command;
    bool _has_nec_command = false;
    bool _ready = false;
    bool _sending = false;
    bool _preferences_dirty = false;
    bool _save_preferences_after_send = false;
    uint32_t _send_started = 0;
    static constexpr uint32_t SEND_TIMEOUT_MS = 2500;
    void render();
    void render_main();
    void render_more();
    void render_compatibility();
    void adjust_parameter(bool forward);
    void send_state(bool power, bool compatibility = false);
    void send_action(factory_ir::CoolixAction action);
    void submit(const std::vector<factory_ir::Pulse>& pulses, const char* completion, bool save_preferences);
    void open_main();
    void open_more();
    void choose_more();
    void open_nec();
    void parse_and_send_ir(const std::string& command);
    void load_preferences();
    bool save_preferences();
};
