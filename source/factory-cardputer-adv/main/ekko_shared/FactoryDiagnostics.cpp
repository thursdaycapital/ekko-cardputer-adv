#include "FactoryDiagnostics.h"
#include "SharedWifiStore.h"
#include "FactoryUi.h"
#include "BoundedSerialWriter.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_task.h>
#include <esp_timer.h>
#include <esp_rom_crc.h>
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <esp_vfs_dev.h>
#include <cJSON.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>
#include <ctime>

namespace {
int selected_index = -1;
const char* selected_name = "";
const char* wifi_setup_state = "closed";
int wifi_setup_networks = 0, wifi_setup_selected = -1;
const char* boot_wifi_state = "not_started";
char command[48] = {};
size_t command_length = 0;
bool overflowed = false;
bool driver_ready = false;

struct ScreenResult {
    const char* status = "not_requested";
    size_t payload_bytes = 0;
    uint32_t elapsed_ms = 0;
    uint32_t crc32 = 0;
} last_screen;

class UsbTransport : public FactoryDiagnostics::SerialTransport {
public:
    int64_t nowUs() override { return esp_timer_get_time(); }
    int write(const uint8_t* data, size_t bytes, uint32_t timeoutUs) override
    {
        return usb_serial_jtag_write_bytes(data, bytes, timeoutUs / (portTICK_PERIOD_MS * 1000));
    }
    bool drain(uint32_t timeoutUs) override
    {
        return usb_serial_jtag_wait_tx_done(timeoutUs / (portTICK_PERIOD_MS * 1000)) == ESP_OK;
    }
    void yield() override { vTaskDelay(1); }
};

class ConsoleFrameLock {
public:
    ConsoleFrameLock()
    {
        // Never wait forever for another task that owns a console stream.
        if (ftrylockfile(stdout) != 0) return;
        if (ftrylockfile(stderr) != 0) { funlockfile(stdout); return; }
        _locked = true;
    }
    ~ConsoleFrameLock()
    {
        if (_locked) { funlockfile(stderr); funlockfile(stdout); }
    }
    bool locked() const { return _locked; }
private:
    bool _locked = false;
};

void health()
{
    if (!driver_ready) return;
    auto* json = cJSON_CreateObject();
    auto* names = cJSON_CreateArray();
    auto* labels = cJSON_CreateArray();
    if (!json || !names || !labels) { cJSON_Delete(json); cJSON_Delete(names); cJSON_Delete(labels); return; }
    const auto* partition = esp_ota_get_running_partition();
    const auto* description = esp_app_get_description();
    wifi_ap_record_t access_point = {};
    const bool associated = esp_wifi_sta_get_ap_info(&access_point) == ESP_OK;
    esp_netif_ip_info_t ip_info = {};
    auto* netif = associated ? esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") : nullptr;
    const bool has_ip = associated && GetHAL().isWifiConnected() && netif &&
                        esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0;
    const char* wifi_state = has_ip ? "connected" : associated ? "getting_ip" :
                             GetHAL().isWifiConnecting() ? "connecting" :
                             GetHAL().isWifiInitialized() ? "disconnected" : "off";
    const char* current_app = "Launcher";
    size_t menu_items = 0;
    auto apps = mooncake::GetMooncake().getAppAbilityManager()->getAllAbilityInstance();
    for (auto* instance : apps) {
        auto* app = static_cast<mooncake::AppAbility*>(instance);
        if (app->getAppInfo().name == "Launcher") continue;
        auto* name = cJSON_CreateString(app->getAppInfo().name.c_str());
        if (!name || !cJSON_AddItemToArray(names, name)) {
            cJSON_Delete(name); cJSON_Delete(names); cJSON_Delete(labels); cJSON_Delete(json); return;
        }
        auto* label = cJSON_CreateString(factory_ui::appTitle(app->getAppInfo().name));
        if (!label || !cJSON_AddItemToArray(labels, label)) {
            cJSON_Delete(label); cJSON_Delete(names); cJSON_Delete(labels); cJSON_Delete(json); return;
        }
        ++menu_items;
        if (mooncake::GetMooncake().getAppCurrentState(app->getId()) == mooncake::AppAbility::StateRunning) {
            current_app = app->getAppInfo().name.c_str();
        }
    }
    cJSON_AddStringToObject(json, "display_language", "zh-CN");
    cJSON_AddStringToObject(json, "display_font", "Fusion Pixel 12px compatible");
    cJSON_AddNumberToObject(json, "display_font_pixels", ekko_pixel_font::kPixels);
    cJSON_AddItemToObject(json, "menu_labels", labels);
    cJSON_AddStringToObject(json, "firmware", description->version);
    cJSON_AddStringToObject(json, "idf", description->idf_ver);
    cJSON_AddStringToObject(json, "partition", partition ? partition->label : "");
    cJSON_AddNumberToObject(json, "partition_address", partition ? partition->address : 0);
    cJSON_AddBoolToObject(json, "wifi_connected", has_ip);
    cJSON_AddBoolToObject(json, "wifi_associated", associated);
    cJSON_AddStringToObject(json, "wifi_state", wifi_state);
    cJSON_AddStringToObject(json, "boot_wifi", boot_wifi_state);
    cJSON_AddNumberToObject(json, "wifi_last_disconnect_reason", GetHAL().wifiLastDisconnectReason());
    cJSON_AddNumberToObject(json, "wifi_disconnect_count", GetHAL().wifiDisconnectCount());
    if (has_ip) {
        char address[16];
        snprintf(address, sizeof(address), IPSTR, IP2STR(&ip_info.ip));
        cJSON_AddStringToObject(json, "ip", address);
        cJSON_AddNumberToObject(json, "wifi_rssi", access_point.rssi);
    }
    cJSON_AddNumberToObject(json, "uptime_ms", esp_timer_get_time() / 1000);
    cJSON_AddNumberToObject(json, "reset_reason", esp_reset_reason());
    auto* stacks = cJSON_AddObjectToObject(json, "stacks");
    if (stacks) {
        cJSON_AddNumberToObject(stacks, "main_allocated_bytes", ESP_TASK_MAIN_STACK);
        cJSON_AddNumberToObject(stacks, "main_min_free_bytes", uxTaskGetStackHighWaterMark(nullptr));
        // Name used by ESP-IDF's default_event_loop.c.
        auto event_task = xTaskGetHandle("sys_evt");
        cJSON_AddNumberToObject(stacks, "event_allocated_bytes", ESP_TASKD_EVENT_STACK);
        if (event_task) {
            cJSON_AddNumberToObject(stacks, "event_min_free_bytes", uxTaskGetStackHighWaterMark(event_task));
        }
    }
    cJSON_AddBoolToObject(json, "shared_wifi_saved", FactoryWifi::hasSharedConfig());
    cJSON_AddNumberToObject(json, "shared_wifi_schema", 1);
    cJSON_AddStringToObject(json, "shared_wifi_reason", FactoryWifi::lastReason());
    cJSON_AddStringToObject(json, "current_app", current_app);
    cJSON_AddNumberToObject(json, "menu_index", selected_index);
    cJSON_AddStringToObject(json, "menu_name", selected_name);
    cJSON_AddNumberToObject(json, "menu_items", menu_items);
    cJSON_AddItemToObject(json, "menu_names", names);
    if (auto* clock = cJSON_AddObjectToObject(json, "clock")) {
        const time_t now = time(nullptr);
        struct tm local = {}, utc = {};
        localtime_r(&now, &local);
        gmtime_r(&now, &utc);
        char local_text[32] = {}, utc_text[32] = {}, offset[8] = {};
        strftime(local_text, sizeof(local_text), "%Y-%m-%d %H:%M:%S", &local);
        strftime(utc_text, sizeof(utc_text), "%Y-%m-%d %H:%M:%S", &utc);
        strftime(offset, sizeof(offset), "%z", &local);
        const char* timezone = std::getenv("TZ");
        cJSON_AddStringToObject(clock, "posix_tz", timezone ? timezone : "");
        cJSON_AddStringToObject(clock, "utc_offset", offset);
        cJSON_AddBoolToObject(clock, "synced", GetHAL().isTimeSynced());
        cJSON_AddNumberToObject(clock, "unix_seconds", static_cast<double>(now));
        cJSON_AddStringToObject(clock, "local", local_text);
        cJSON_AddStringToObject(clock, "utc", utc_text);
    }
    if (std::strcmp(current_app, "SetWiFi") == 0) {
        auto* setup = cJSON_AddObjectToObject(json, "wifi_setup");
        if (setup) {
            cJSON_AddStringToObject(setup, "state", wifi_setup_state);
            cJSON_AddNumberToObject(setup, "networks", wifi_setup_networks);
            cJSON_AddNumberToObject(setup, "selected", wifi_setup_selected);
        }
    }
    auto* screenshot = cJSON_AddObjectToObject(json, "last_screen");
    if (screenshot) {
        cJSON_AddStringToObject(screenshot, "status", last_screen.status);
        cJSON_AddNumberToObject(screenshot, "payload_bytes_queued", last_screen.payload_bytes);
        cJSON_AddNumberToObject(screenshot, "expected_bytes", 240 * 135 * 2);
        cJSON_AddNumberToObject(screenshot, "elapsed_ms", last_screen.elapsed_ms);
        cJSON_AddNumberToObject(screenshot, "crc32", last_screen.crc32);
    }
    char* text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!text) return;
    const std::string response = std::string("FACTORY_HEALTH ") + text + "\n";
    cJSON_free(text);
    ConsoleFrameLock lock;
    if (!lock.locked()) return;
    UsbTransport transport;
    FactoryDiagnostics::BoundedSerialWriter writer(transport, 2000000);
    if (writer.writeAll(response.data(), response.size())) writer.finish();
}

void screen()
{
    last_screen = ScreenResult();
    auto& hal = GetHAL();
    if (hal.display.width() != 240 || hal.display.height() != 135) {
        last_screen.status = "screen_dimensions";
        return;
    }
    if (!driver_ready) { last_screen.status = "driver_unavailable"; return; }
    ConsoleFrameLock lock;
    if (!lock.locked()) { last_screen.status = "console_busy"; return; }
    UsbTransport transport;
    FactoryDiagnostics::BoundedSerialWriter writer(transport, 4000000);
    constexpr char header[] = "FACTORY_SCREEN 240 135 64800 RGB565LE\n";
    bool complete = writer.writeAll(header, sizeof(header) - 1);
    uint8_t row[480];
    for (int y = 0; complete && y < 135; ++y) {
        for (int x = 0; x < 240; ++x) {
            uint16_t pixel;
            if (x < hal.canvasKeyboardBar.width()) {
                pixel = hal.canvasKeyboardBar.readPixel(x, y);
            } else if (y < hal.canvasSystemBar.height()) {
                pixel = hal.canvasSystemBar.readPixel(x - hal.canvasKeyboardBar.width(), y);
            } else {
                pixel = hal.canvas.readPixel(x - hal.canvasKeyboardBar.width(), y - hal.canvasSystemBar.height());
            }
            row[x * 2] = pixel & 0xff;
            row[x * 2 + 1] = pixel >> 8;
        }
        const size_t before = writer.acceptedBytes();
        complete = writer.writeAll(row, sizeof(row));
        const size_t queued = writer.acceptedBytes() - before;
        last_screen.payload_bytes += queued;
        last_screen.crc32 = esp_rom_crc32_le(last_screen.crc32, row, queued);
    }
    if (complete) {
        char trailer[64];
        const int bytes = snprintf(trailer, sizeof(trailer), "\nFACTORY_SCREEN_END 64800 %08lx\n",
                                   static_cast<unsigned long>(last_screen.crc32));
        complete = bytes > 0 && static_cast<size_t>(bytes) < sizeof(trailer) &&
                   writer.writeAll(trailer, static_cast<size_t>(bytes)) && writer.finish();
    }
    last_screen.status = complete ? "complete" : writer.error();
    last_screen.elapsed_ms = writer.elapsedUs() / 1000;
    // No error text is inserted into an incomplete raw payload. A subsequent
    // FACTORY_HEALTH reports the bounded timeout and exact queued byte count.
}
}

void factoryDiagnosticsSetSelection(int index, const char* name)
{
    selected_index = index;
    selected_name = name;
}

void factoryDiagnosticsSetWifiSetup(const char* state, int networks, int selected)
{
    wifi_setup_state = state;
    wifi_setup_networks = networks;
    wifi_setup_selected = selected;
}

void factoryDiagnosticsSetBootWifi(const char* state)
{
    boot_wifi_state = state;
}

void factoryDiagnosticsInit()
{
    fflush(stdout);
    fflush(stderr);
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = {2048, 256};
        driver_ready = usb_serial_jtag_driver_install(&config) == ESP_OK;
    } else {
        driver_ready = true;
    }
    if (driver_ready) usb_serial_jtag_vfs_use_driver();
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
    const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

void factoryDiagnosticsPoll()
{
    char input[64];
    const int length = read(STDIN_FILENO, input, sizeof(input));
    for (int i = 0; i < length; ++i) {
        const char c = input[i];
        if (c == '\r' || c == '\n') {
            command[command_length] = '\0';
            if (!overflowed && command_length) {
                if (std::strcmp(command, "FACTORY_HEALTH") == 0) health();
                else if (std::strcmp(command, "FACTORY_SCREEN") == 0) screen();
                else puts("FACTORY_ERROR unsupported_command");
            }
            command_length = 0;
            overflowed = false;
        } else if (command_length + 1 < sizeof(command) && !overflowed) {
            command[command_length++] = c;
        } else {
            overflowed = true;
        }
    }
}
