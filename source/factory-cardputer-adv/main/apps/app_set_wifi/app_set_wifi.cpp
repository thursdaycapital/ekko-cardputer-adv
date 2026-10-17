/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#include "app_set_wifi.h"
#include "assets/set_wifi_big.h"
#include "assets/set_wifi_small.h"
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <ekko_shared/SharedWifiStore.h>
#include <ekko_shared/FactoryDiagnostics.h>
#include <mooncake_log.h>
#include <cstring>

namespace {
constexpr uint32_t BACKGROUND = 0x242B28;
constexpr uint32_t ACCENT = 0xD3E8A1;
constexpr uint32_t MUTED = 0xA5B5A9;
constexpr int FIELD_Y = 44;
}

AppSetWiFi::AppSetWiFi()
{
    setAppInfo().name = "SetWiFi";
    setAppInfo().userData = new AppIcon_t(image_data_set_wifi_big, image_data_set_wifi_small);
}
AppSetWiFi::~AppSetWiFi() { delete static_cast<AppIcon_t*>(getAppInfo().userData); }

void AppSetWiFi::onOpen()
{
    _model = WifiSetup::Model();
    SharedWifiConfig::Credentials saved = {};
    if (FactoryWifi::load(saved)) _model.loadSaved(saved.ssid, saved.password);
    _opened_at = _cursor_time = GetHAL().millis();
    _cursor = true;
    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& event) { handle_key_event(event); });
}

void AppSetWiFi::onRunning()
{
    if (GetHAL().homeButton.wasClicked()) { close(); return; }
    // Wait for the original launcher's app-open animation to finish.
    if (GetHAL().millis() - _opened_at < 250) return;
    if (_model.state == WifiSetup::Model::Scanning) {
        render();
        GetHAL().wifiInit();
        std::vector<Hal::ScanResult_t> results;
        const bool ok = GetHAL().wifiScan(results);
        _model.setNetworks(results, ok);
        render();
    } else if (_model.state == WifiSetup::Model::Connecting) {
        const bool connected = GetHAL().wifiConnect(_model.ssid, _model.password);
        const bool committed = connected && FactoryWifi::save(_model.ssid.c_str(), _model.password.c_str()) == ESP_OK;
        _model.connectionResult(connected, committed);
        render();
    } else if ((_model.state == WifiSetup::Model::Ssid || _model.state == WifiSetup::Model::Password) &&
               GetHAL().millis() - _cursor_time >= 500) {
        _cursor_time = GetHAL().millis();
        _cursor = !_cursor;
        render_input();
        GetHAL().pushCanvas();
    }
}

void AppSetWiFi::onClose()
{
    if (_key_event_slot_id >= 0) GetHAL().keyboard.onKeyEvent.disconnect(_key_event_slot_id);
    _key_event_slot_id = -1;
    _model = WifiSetup::Model();
    GetHAL().canvas.setTextWrap(true);
}

void AppSetWiFi::text(int x, int y, const std::string& value, int width, uint32_t color, bool tail)
{
    auto& canvas = GetHAL().canvas;
    std::string shown = value;
    bool trimmed = false;
    while (!shown.empty() && canvas.textWidth((shown + (trimmed && !tail ? ".." : "")).c_str()) > width) {
        if (tail) {
            size_t end = 1;
            while (end < shown.size() && (static_cast<unsigned char>(shown[end]) & 0xc0) == 0x80) ++end;
            shown.erase(0, end);
        } else WifiSetup::eraseLastCharacter(shown);
        trimmed = true;
    }
    if (trimmed && !tail) shown += "..";
    canvas.setTextColor(color);
    canvas.drawString(shown.c_str(), x, y);
}

void AppSetWiFi::footer(const char* first, const char* second)
{
    auto& canvas = GetHAL().canvas;
    canvas.setFont(&fonts::Font0);
    text(4, 87, first, canvas.width() - 8, MUTED);
    text(4, 99, second, canvas.width() - 8, MUTED);
    canvas.setFont(FONT_BASIC);
}

void AppSetWiFi::render()
{
    static constexpr const char* states[] = {"scanning", "selecting", "ssid", "password", "connecting", "success", "failed"};
    factoryDiagnosticsSetWifiSetup(states[_model.state], _model.networks.size(), _model.networks.empty() ? -1 : _model.selected);
    auto& canvas = GetHAL().canvas;
    canvas.setFont(FONT_BASIC);
    canvas.setTextSize(1);
    canvas.setTextDatum(lgfx::textdatum_t::top_left);
    canvas.setTextScroll(false);
    canvas.setTextWrap(false);
    canvas.fillScreen(BACKGROUND);
    const int width = canvas.width();
    using Model = WifiSetup::Model;
    switch (_model.state) {
    case Model::Scanning:
        text(4, 4, "搜索 Wi-Fi", width - 8, ACCENT);
        text(4, 35, "正在扫描附近网络…", width - 8, TFT_WHITE);
        footer("2.4 GHz Wi-Fi", "G0: Exit");
        break;
    case Model::Selecting: {
        text(4, 1, "选择 Wi-Fi", width - 8, ACCENT);
        if (_model.networks.empty()) {
            text(4, 28, _model.scanOk ? "未发现附近网络" : "扫描失败，请重试", width - 8, TFT_WHITE);
            text(4, 52, "也可手动输入名称", width - 8, MUTED);
        } else {
            const size_t first = (_model.selected / 3) * 3;
            for (size_t row = 0; row < 3 && first + row < _model.networks.size(); ++row) {
                const size_t i = first + row;
                const auto& network = _model.networks[i];
                const int y = 23 + row * 20;
                const bool active = i == _model.selected;
                if (active) canvas.fillRoundRect(2, y - 1, width - 4, 19, 3, ACCENT);
                text(6, y, network.ssid, width - 34, active ? BACKGROUND : TFT_WHITE);
                canvas.setFont(&fonts::Font0);
                text(width - 26, y + 5, _model.isSaved(network.ssid) ? "*" : std::to_string(network.rssi), 24,
                     active ? BACKGROUND : MUTED);
                canvas.setFont(FONT_BASIC);
            }
        }
        footer(";/. Select  OK: Password", "R: Scan  M: Manual  G0: Exit");
        break;
    }
    case Model::Ssid:
    case Model::Password:
        text(4, 2, _model.state == Model::Ssid ? "手动输入网络名" : "输入 Wi-Fi 密码", width - 8, ACCENT);
        text(4, 23, _model.state == Model::Ssid ? "名称区分大小写" : _model.ssid, width - 8, MUTED);
        render_input();
        canvas.setFont(&fonts::Font0);
        text(4, 73, _model.state == Model::Password ? "Empty password = open Wi-Fi" : "SSID: up to 32 bytes", width - 8, MUTED);
        footer("OK: Connect/Next  Tab: Clear", "Fn+Esc: Networks  G0: Exit");
        break;
    case Model::Connecting:
        text(4, 4, "正在连接…", width - 8, ACCENT);
        text(4, 34, _model.ssid, width - 8, TFT_WHITE);
        footer("Please wait...", "");
        break;
    case Model::Success:
        text(4, 4, "已连接并保存", width - 8, ACCENT);
        text(4, 31, _model.ssid, width - 8, TFT_WHITE);
        text(4, 55, "EkkoStudio 将使用此网络", width - 8, MUTED);
        footer("Fn+Esc: Choose another Wi-Fi", "OK / G0: Return to menu");
        break;
    case Model::Failed:
        text(4, 4, _model.saveFailed ? "已连接，保存失败" : "连接失败", width - 8, TFT_ORANGE);
        text(4, 31, _model.ssid, width - 8, TFT_WHITE);
        text(4, 55, _model.saveFailed ? "请重试保存网络" : "请检查密码或网络", width - 8, MUTED);
        footer("OK: Edit password / Retry", "Fn+Esc: Networks  G0: Exit");
        break;
    }
    GetHAL().pushCanvas();
}

void AppSetWiFi::render_input()
{
    auto& canvas = GetHAL().canvas;
    canvas.setFont(FONT_BASIC);
    // Fixed coordinates: blinking cannot wrap/scroll the SSID or duplicate it.
    canvas.fillRoundRect(2, FIELD_Y, canvas.width() - 4, 25, 3, BACKGROUND);
    canvas.drawRoundRect(2, FIELD_Y, canvas.width() - 4, 25, 3, MUTED);
    const auto shown = _model.state == WifiSetup::Model::Password ? std::string(_model.input.size(), '*') : _model.input;
    text(7, FIELD_Y + 4, shown + (_cursor ? "_" : " "), canvas.width() - 14, TFT_WHITE, true);
}

void AppSetWiFi::handle_key_event(const Keyboard::KeyEvent_t& event)
{
    if (!event.state || event.isModifier || GetHAL().millis() - _opened_at < 250) return;
    // Modifier chords must not accidentally type their letter into a password.
    if (GetHAL().keyboard.getModifierMask() & (KEY_MOD_LCTRL | KEY_MOD_RCTRL | KEY_MOD_LALT | KEY_MOD_RALT | KEY_MOD_LMETA | KEY_MOD_RMETA)) return;
    using Model = WifiSetup::Model;
    const auto key = event.keyCode;
    if (_model.state == Model::Scanning || _model.state == Model::Connecting) return;
    if (key == KEY_ESC) {
        if (_model.state == Model::Selecting) { close(); return; }
        _model.input.clear();
        _model.state = Model::Selecting;
    } else if (_model.state == Model::Selecting) {
        if (key == KEY_UP || key == KEY_LEFT || key == KEY_SEMICOLON || key == KEY_COMMA) _model.move(-1);
        else if (key == KEY_DOWN || key == KEY_RIGHT || key == KEY_DOT || key == KEY_SLASH) _model.move(1);
        else if (key == KEY_R) _model.state = Model::Scanning;
        else if (key == KEY_M) _model.manual();
        else if (key == KEY_ENTER) _model.choose();
        else return;
    } else if (key == KEY_ENTER) {
        if (_model.state == Model::Success) { close(); return; }
        _model.submit();
    } else if (_model.state == Model::Ssid || _model.state == Model::Password) {
        if (key == KEY_TAB) _model.input.clear();
        else if (key == KEY_BACKSPACE) WifiSetup::eraseLastCharacter(_model.input);
        else if (event.keyName && std::strlen(event.keyName) == 1) _model.append(event.keyName);
        else return;
    } else return;
    _cursor = true;
    _cursor_time = GetHAL().millis();
    render();
}
