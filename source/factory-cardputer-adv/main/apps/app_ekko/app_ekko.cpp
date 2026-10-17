#include "app_ekko.h"
#include "assets/ekko_big.h"
#include "assets/ekko_small.h"
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <hal/hal.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <cstring>

AppEkko::AppEkko()
{
    setAppInfo().name = "EkkoStudio";
    setAppInfo().userData = new AppIcon_t(image_data_ekko_big, image_data_ekko_small);
}

AppEkko::~AppEkko()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppEkko::onOpen()
{
    _opened_at = GetHAL().millis();
    _attempted = false;
}

void AppEkko::show_status(const char* message, bool error)
{
    auto& canvas = GetHAL().canvas;
    canvas.fillScreen(THEME_COLOR_BG);
    canvas.setFont(FONT_BASIC);
    canvas.setTextSize(1);
    canvas.setTextScroll(false);
    canvas.setTextColor(error ? TFT_RED : TFT_WHITE, THEME_COLOR_BG);
    canvas.setCursor(3, 5);
    canvas.println("EkkoStudio");
    canvas.println();
    canvas.setFont(&fonts::Font0);
    canvas.println(message);
    if (error) {
        canvas.println();
        canvas.println("Home: return to menu");
    }
    GetHAL().pushCanvas();
}

void AppEkko::onRunning()
{
    if (GetHAL().homeButton.wasClicked()) {
        close();
        return;
    }
    // The original launcher finishes its open animation after onOpen().
    if (!_attempted && GetHAL().millis() - _opened_at >= 200) {
        _attempted = true;
        launch();
    }
}

void AppEkko::launch()
{
    show_status("Checking installed image...");
    const auto* target = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                 ESP_PARTITION_SUBTYPE_APP_OTA_0, "app0");
    if (!target || target->address != 0x10000 || target->size != 0x330000) {
        show_status("Ekko partition missing.\nInstall the dual-app package.", true);
        return;
    }

    esp_app_desc_t description = {};
    if (esp_ota_get_partition_description(target, &description) != ESP_OK) {
        show_status("Ekko image unavailable.\nReinstall the Ekko image.", true);
        return;
    }
    if (std::strncmp(description.project_name, "arduino-lib-builder", sizeof(description.project_name)) != 0) {
        show_status("Unexpected app in Ekko slot.\nReinstall the Ekko image.", true);
        return;
    }
    if (GetHAL().getSettings().Commit() != ESP_OK) {
        show_status("Settings could not be saved.\nShared NVS was preserved.", true);
        return;
    }
    // ESP-IDF verifies the complete image before writing OTA selection. A bad
    // image leaves the current factory app and its NVS untouched.
    const auto result = esp_ota_set_boot_partition(target);
    if (result != ESP_OK) {
        show_status(esp_err_to_name(result), true);
        return;
    }
    show_status("Starting EkkoStudio...");
    GetHAL().delay(180);
    esp_restart();
}
