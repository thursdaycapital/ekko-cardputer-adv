/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_wifi_scan.h"
#include "assets/scan_big.h"
#include "assets/scan_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <ekko_shared/FactoryUi.h>
#include <mooncake_log.h>
#include <assets.h>

using namespace mooncake;

AppWifiScan::AppWifiScan()
{
    setAppInfo().name     = "Scan";
    setAppInfo().userData = new AppIcon_t(image_data_scan_big, image_data_scan_small);
    _time_count           = 0;
}

AppWifiScan::~AppWifiScan()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppWifiScan::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    GetHAL().wifiInit();
    render_page_scanning();
}

void AppWifiScan::onRunning()
{
    // Scan in every 6 seconds
    if (GetHAL().millis() - _time_count > 5000) {
        _scan_ok = GetHAL().wifiScan(_scan_result);
        render_page_result();
        _time_count = GetHAL().millis();
    }

    // Close app when home button clicked
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppWifiScan::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    GetHAL().canvas.setTextWrap(true);
}

void AppWifiScan::render_page_scanning()
{
    auto& canvas = GetHAL().canvas;
    canvas.setBaseColor(THEME_COLOR_BG);
    canvas.setFont(FONT_BASIC);
    canvas.setTextScroll(false);
    canvas.setTextWrap(false);
    canvas.setTextSize(1);
    canvas.setTextDatum(lgfx::textdatum_t::top_left);
    canvas.fillScreen(THEME_COLOR_BG);
    canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    canvas.drawString("Wi-Fi 扫描", 0, 0);
    canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    canvas.drawString("正在扫描附近网络…", 0, 28);
    canvas.setTextColor(TFT_DARKGRAY, THEME_COLOR_BG);
    canvas.drawString("5秒自动刷新  G0:退出", 0, 96);
    GetHAL().pushCanvas();
}

void AppWifiScan::render_page_result()
{
    auto& canvas = GetHAL().canvas;
    canvas.setFont(FONT_BASIC);
    canvas.setTextSize(1);
    canvas.setTextWrap(false);
    canvas.setTextScroll(false);
    canvas.setTextDatum(lgfx::textdatum_t::top_left);
    canvas.fillScreen(THEME_COLOR_BG);
    canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    canvas.drawString("Wi-Fi 扫描", 0, 0);

    if (!_scan_ok) {
        canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
        canvas.drawString("扫描失败，稍后重试", 0, 28);
    } else if (_scan_result.empty()) {
        canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
        canvas.drawString("未发现 Wi-Fi 网络", 0, 28);
    } else {
        int row = 0;
        for (const auto& result : _scan_result) {
            if (row >= 6) {
                break;
            }

            mclog::tagInfo(getAppInfo().name, "found: {} {}", result.second, result.first);

            // Set color based on signal strength
            if (result.first > -60) {
                canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
            } else if (result.first > -70) {
                canvas.setTextColor(TFT_YELLOW, THEME_COLOR_BG);
            } else {
                canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
            }

            const int y = 14 + 13 * row++;
            canvas.drawString(factory_ui::fitText(canvas, std::to_string(result.first), 30).c_str(), 0, y);
            canvas.drawString(factory_ui::fitText(canvas, result.second, canvas.width() - 36).c_str(), 36, y);
        }
    }

    canvas.setTextColor(TFT_DARKGRAY, THEME_COLOR_BG);
    canvas.drawString("5秒自动刷新  G0:退出", 0, 96);
    GetHAL().pushCanvas();
}
