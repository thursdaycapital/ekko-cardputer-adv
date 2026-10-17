#include "SharedWifiStore.h"
#include <nvs.h>
#include <cstring>

namespace FactoryWifi {
namespace {
const char* reason = "not_loaded";
esp_err_t get_string(nvs_handle_t handle, const char* key, char* value, size_t capacity)
{
    size_t length = capacity;
    return nvs_get_str(handle, key, value, &length);
}
bool get_legacy(const char* name, const char* ssid_key, const char* password_key,
                SharedWifiConfig::Credentials& credentials)
{
    nvs_handle_t handle = 0;
    if (nvs_open(name, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    SharedWifiConfig::Credentials loaded = {};
    const bool have_ssid = get_string(handle, ssid_key, loaded.ssid, sizeof(loaded.ssid)) == ESP_OK;
    // A missing password is valid for open networks.
    const auto password_result = get_string(handle, password_key, loaded.password, sizeof(loaded.password));
    nvs_close(handle);
    uint8_t blob[SharedWifiConfig::kBlobSize];
    if (!have_ssid || (password_result != ESP_OK && password_result != ESP_ERR_NVS_NOT_FOUND) ||
        !SharedWifiConfig::encode(loaded.ssid, loaded.password, blob, sizeof(blob))) {
        return false;
    }
    credentials = loaded;
    return true;
}
enum class RecordState { Missing, Valid, Invalid };
RecordState get_shared(SharedWifiConfig::Credentials& credentials)
{
    nvs_handle_t handle = 0;
    const auto open_result = nvs_open(SharedWifiConfig::kNvsNamespace, NVS_READONLY, &handle);
    if (open_result != ESP_OK) {
        return open_result == ESP_ERR_NVS_NOT_FOUND ? RecordState::Missing : RecordState::Invalid;
    }
    uint8_t blob[SharedWifiConfig::kBlobSize];
    size_t length = sizeof(blob);
    const auto result = nvs_get_blob(handle, SharedWifiConfig::kNvsKey, blob, &length);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) return RecordState::Missing;
    return result == ESP_OK && SharedWifiConfig::decode(blob, length, credentials)
        ? RecordState::Valid : RecordState::Invalid;
}
}

bool load(SharedWifiConfig::Credentials& credentials)
{
    const auto shared_state = get_shared(credentials);
    if (shared_state == RecordState::Valid) {
        reason = "shared_v1";
        return true;
    }
    if (shared_state == RecordState::Invalid) {
        credentials = SharedWifiConfig::Credentials();
        reason = "shared_invalid";
        return false;
    }
    if (get_legacy("cardputer", "wifi_ssid", "wifi_password", credentials)) {
        reason = "factory_legacy";
        return true;
    }
    if (get_legacy("net", "ssid", "pass", credentials)) {
        reason = "ekko_initial_migration";
        return true;
    }
    reason = "no_saved_network";
    return false;
}

esp_err_t save(const char* ssid, const char* password)
{
    uint8_t blob[SharedWifiConfig::kBlobSize];
    if (!SharedWifiConfig::encode(ssid, password, blob, sizeof(blob))) {
        reason = "invalid_credentials";
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(SharedWifiConfig::kNvsNamespace, NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        // One authoritative record. Do not mirror separate legacy keys: NVS
        // commit is not a transaction spanning multiple set calls, so a failed
        // first save must never leave a new SSID paired with an old password.
        result = nvs_set_blob(handle, SharedWifiConfig::kNvsKey, blob, sizeof(blob));
        if (result == ESP_OK) result = nvs_commit(handle);
        nvs_close(handle);
    }
    reason = result == ESP_OK ? "saved_v1" : "save_failed";
    return result;
}

bool hasSharedConfig()
{
    SharedWifiConfig::Credentials credentials = {};
    return get_shared(credentials) == RecordState::Valid;
}
const char* lastReason() { return reason; }
}
