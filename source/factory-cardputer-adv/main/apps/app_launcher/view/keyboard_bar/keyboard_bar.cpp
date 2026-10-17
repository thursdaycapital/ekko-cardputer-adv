/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "../../app_launcher.h"
#include <mooncake_log.h>
#include <apps/utils/theme.h>
#include <apps/utils/common.h>

void Launcher::render_keyboard_bar()
{
    // Backgound
    int margin_x = 4;
    int margin_y = 6;

    GetHAL().canvasKeyboardBar.fillScreen(THEME_COLOR_BG);
    GetHAL().canvasKeyboardBar.fillSmoothRoundRect(
        margin_x, margin_y, GetHAL().canvasKeyboardBar.width() - margin_x * 2,
        GetHAL().canvasKeyboardBar.height() - margin_y * 2, (GetHAL().canvasKeyboardBar.height() - margin_y * 2) / 2,
        THEME_COLOR_KB_BAR);

    // Use the same native pixel font as every menu and application label.
    auto& canvas = GetHAL().canvasKeyboardBar;
    canvas.setFont(FONT_BASIC);
    canvas.setTextSize(1);
    const char* labels[] = {"Aa", "Fn", "Ctrl", "Opt", "Alt"};
    const bool active[] = {_data.keyboard_state.shift, _data.keyboard_state.fn,
        _data.keyboard_state.ctrl, _data.keyboard_state.opt, _data.keyboard_state.alt};
    const uint32_t colors[] = {THEME_COLOR_KB_CAPS_LOCK, THEME_COLOR_KB_FN,
        THEME_COLOR_KB_CTRL, THEME_COLOR_KB_OPT, THEME_COLOR_KB_ALT};
    for (int i = 0; i < 5; ++i) {
        const int top = 20 + i * 20;
        const uint32_t bg = active[i] ? colors[i] : THEME_COLOR_KB_BAR_ICON_BG;
        canvas.fillRoundRect(4, top, canvas.width() - 8, 17, 3, bg);
        canvas.setTextColor(active[i] ? TFT_BLACK : THEME_COLOR_KB_BAR_ICON_TEXT, bg);
        canvas.drawCenterString(labels[i], canvas.width() / 2, top + 2);
    }

    // Push
    GetHAL().pushCanvasKeyboardBar();
}

void Launcher::start_keyboard_bar()
{
    GetHAL().keyboard.onKeyEvent.connect([this](const Keyboard::KeyEvent_t& keyEvent) {
        bool need_render = false;
        // Fn key emits KEY_NONE with isModifier - handle before the switch
        if (keyEvent.keyCode == KEY_NONE && keyEvent.isModifier) {
            _data.keyboard_state.fn = keyEvent.state;
            need_render             = true;
        } else {
            switch (keyEvent.keyCode) {
                case KEY_LEFTSHIFT:
                    _data.keyboard_state.shift = keyEvent.state;
                    need_render                = true;
                    break;
                case KEY_LEFTCTRL:
                    _data.keyboard_state.ctrl = keyEvent.state;
                    need_render               = true;
                    break;
                case KEY_LEFTMETA:
                    _data.keyboard_state.opt = keyEvent.state;
                    need_render              = true;
                    break;
                case KEY_LEFTALT:
                    _data.keyboard_state.alt = keyEvent.state;
                    need_render              = true;
                    break;
                default:
                    break;
            }
        }

        if (need_render) {
            render_keyboard_bar();
        }
    });

    render_keyboard_bar();
}
