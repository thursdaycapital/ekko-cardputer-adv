#include "SharedWifiStore.h"
#include <nvs.h>
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;
using Store = std::map<std::string, std::map<std::string, Bytes>>;
Store data;
std::map<nvs_handle_t, std::string> handles;
std::string fail_key;
bool fail_commit = false;
int commits = 0, blobs = 0;
void set_string(const char* space, const char* key, const char* value) {
    data[space][key] = Bytes(value, value + std::strlen(value) + 1);
}
void reset() { data.clear(); handles.clear(); fail_key.clear(); fail_commit = false; commits = blobs = 0; }
}

esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* handle) {
    if (mode == NVS_READONLY && !data.count(name)) return ESP_ERR_NVS_NOT_FOUND;
    *handle = handles.size() + 1;
    while (handles.count(*handle)) ++*handle;
    handles[*handle] = name;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* output, size_t* length) {
    const auto& values = data[handles.at(handle)];
    auto value = values.find(key);
    if (value == values.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (*length < value->second.size()) { *length = value->second.size(); return ESP_ERR_NVS_INVALID_LENGTH; }
    *length = value->second.size();
    std::memcpy(output, value->second.data(), *length);
    return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t h, const char* k, char* v, size_t* n) { return nvs_get_blob(h, k, v, n); }
esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t length) {
    if (fail_key == key) return ESP_FAIL;
    ++blobs;
    const auto* bytes = static_cast<const uint8_t*>(value);
    data[handles.at(handle)][key] = Bytes(bytes, bytes + length);
    return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t handle, const char* key, const char* value) {
    if (fail_key == key) return ESP_FAIL;
    set_string(handles.at(handle).c_str(), key, value);
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t) { ++commits; return fail_commit ? ESP_FAIL : ESP_OK; }
void nvs_close(nvs_handle_t handle) { handles.erase(handle); }

int main() {
    SharedWifiConfig::Credentials result;
    reset();
    assert(!FactoryWifi::load(result));
    set_string("net", "ssid", "initial-network");
    set_string("net", "pass", "initial-key");
    assert(FactoryWifi::load(result) && std::strcmp(result.ssid, "initial-network") == 0);
    assert(!FactoryWifi::hasSharedConfig()); // Reads never publish stale fallbacks.
    set_string("cardputer", "wifi_ssid", "factory-network");
    set_string("cardputer", "wifi_password", "factory-key");
    assert(FactoryWifi::load(result) && std::strcmp(result.ssid, "factory-network") == 0);
    fail_key = "ekko_wifi_v1";
    assert(FactoryWifi::save("failed-first-network", "failed-first-key") == ESP_FAIL);
    assert(FactoryWifi::load(result) && std::strcmp(result.ssid, "factory-network") == 0);
    assert(std::strcmp(result.password, "factory-key") == 0);
    assert(commits == 0 && blobs == 0);
    fail_key.clear();
    assert(FactoryWifi::save("new-network", "new-key") == ESP_OK);
    assert(commits == 1 && blobs == 1 && FactoryWifi::hasSharedConfig());
    assert(FactoryWifi::load(result) && std::strcmp(result.ssid, "new-network") == 0);
    assert(std::strcmp(result.password, "new-key") == 0);
    assert(std::string(reinterpret_cast<const char*>(data["cardputer"]["wifi_ssid"].data())) == "factory-network");
    const auto old_blob = data["cardputer"]["ekko_wifi_v1"];
    fail_key = "ekko_wifi_v1";
    assert(FactoryWifi::save("other-network", "other-key") == ESP_FAIL);
    assert(data["cardputer"]["ekko_wifi_v1"] == old_blob && commits == 1);
    assert(FactoryWifi::load(result) && std::strcmp(result.ssid, "new-network") == 0);
    fail_key.clear(); fail_commit = true;
    assert(FactoryWifi::save("network", "key") == ESP_FAIL);
    assert(std::strcmp(FactoryWifi::lastReason(), "save_failed") == 0);
    reset();
    fail_key = "ekko_wifi_v1";
    assert(FactoryWifi::save("failed-new-network", "failed-new-key") == ESP_FAIL);
    assert(!data["cardputer"].count("wifi_ssid") && !data["cardputer"].count("wifi_password"));
    assert(!FactoryWifi::load(result));
    reset();
    set_string("cardputer", "wifi_ssid", "old-network");
    set_string("cardputer", "wifi_password", "old-key");
    data["cardputer"]["ekko_wifi_v1"] = Bytes{1,2,3};
    assert(!FactoryWifi::load(result)); // Present but invalid records block stale fallback.
    assert(std::strcmp(FactoryWifi::lastReason(), "shared_invalid") == 0);
    reset();
    assert(FactoryWifi::save("open-network", "") == ESP_OK);
    assert(FactoryWifi::load(result) && result.password[0] == '\0');
    assert(FactoryWifi::save("", "key") == ESP_ERR_INVALID_ARG);
    reset();
    set_string("cardputer", "wifi_ssid", "network");
    set_string("cardputer", "wifi_password", std::string(80, 'x').c_str());
    assert(!FactoryWifi::load(result)); // Oversized stored passwords are rejected.
}
