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
    canvas.setFont(&ekko_pixel_font::kFont);
    canvas.println(message);
    if (error) {
        canvas.println();
        canvas.println("顶部 G0：返回菜单");
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
    show_status("正在检查 EkkoStudio…");
    const auto* target = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                 ESP_PARTITION_SUBTYPE_APP_OTA_0, "app0");
    if (!target || target->address != 0x10000 || target->size != 0x330000) {
        show_status("缺少 EkkoStudio 分区\n请安装双固件完整包", true);
        return;
    }

    esp_app_desc_t description = {};
    if (esp_ota_get_partition_description(target, &description) != ESP_OK) {
        show_status("无法读取 EkkoStudio\n请重新安装该固件", true);
        return;
    }
    if (std::strncmp(description.project_name, "arduino-lib-builder", sizeof(description.project_name)) != 0) {
        show_status("EkkoStudio 分区内容不符\n请重新安装该固件", true);
        return;
    }
    if (GetHAL().getSettings().Commit() != ESP_OK) {
        show_status("设置保存失败\n原有联网设置已保留", true);
        return;
    }
    // ESP-IDF verifies the complete image before writing OTA selection. A bad
    // image leaves the current factory app and its NVS untouched.
    const auto result = esp_ota_set_boot_partition(target);
    if (result != ESP_OK) {
        const std::string detail = std::string("切换固件失败\n错误代码：") + esp_err_to_name(result);
        show_status(detail.c_str(), true);
        return;
    }
    show_status("正在启动 EkkoStudio…");
    GetHAL().delay(180);
    esp_restart();
}
