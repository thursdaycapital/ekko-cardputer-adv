/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_launcher.h"
#include <apps/utils/theme.h>
#include <apps/utils/common.h>
#include <mooncake_log.h>
#include <hal/hal.h>
#include <hal.h>
#include <ekko_shared/SharedWifiStore.h>
#include <ekko_shared/FactoryDiagnostics.h>

using namespace mooncake;

void Launcher::onCreate()
{
    setAppInfo().name = "Launcher";
    mclog::tagInfo(getAppInfo().name, "on create");

    // Init
    // Finish one saved-network attempt before interactive radio demos can run.
    // This also runs before the original power-on logo waits for a key.
    connect_saved_wifi();
    boot_anim();
    start_menu();
    start_system_bar();
    start_keyboard_bar();

    open();
}

void Launcher::connect_saved_wifi()
{
    SharedWifiConfig::Credentials saved = {};
    if (!FactoryWifi::load(saved)) {
        factoryDiagnosticsSetBootWifi("no_saved_network");
        return;
    }
    factoryDiagnosticsSetBootWifi("connecting");
    auto& display = GetHAL().display;
    display.fillScreen(THEME_COLOR_BG);
    display.setFont(FONT_BASIC);
    display.setTextSize(1);
    display.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    display.drawCenterString("正在连接已保存网络", display.width() / 2, 45);
    display.drawCenterString("请稍候…", display.width() / 2, 72);
    display.setBrightness(255);

    const bool connected = GetHAL().wifiConnect(saved.ssid, saved.password);
    factoryDiagnosticsSetBootWifi(connected ? "connected" : "failed");
    if (!connected) {
        // Failure never erases credentials or prevents entry to SetWiFi.
        display.fillScreen(THEME_COLOR_BG);
        display.drawCenterString("暂未连接", display.width() / 2, 45);
        display.drawCenterString("可在“网络设置”更换网络", display.width() / 2, 72);
        GetHAL().delay(700);
    }
}

void Launcher::onRunning()
{
    // mclog::tagInfo(getAppInfo().name, "on running");

    update_system_bar();

    // If app is opened and running
    if (_data.running_app_id >= 0) {
        // If running app is closed
        if (GetMooncake().getAppCurrentState(_data.running_app_id) == AppAbility::StateSleeping) {
            _data.running_app_id = -1;
            ANIM_APP_CLOSE();
        }
    } else {
        update_menu();
    }
}
