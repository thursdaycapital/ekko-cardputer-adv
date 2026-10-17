#pragma once
#include <esp_err.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
using nvs_handle_t = uint32_t;
enum nvs_open_mode_t { NVS_READONLY, NVS_READWRITE };
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;
constexpr esp_err_t ESP_ERR_NVS_INVALID_LENGTH = 0x110c;
namespace remote_nvs_test {
struct State {
    std::map<std::string, std::map<std::string, std::vector<uint8_t>>> namespaces;
    std::map<nvs_handle_t, std::string> handles;
    std::map<nvs_handle_t, std::map<std::string, std::vector<uint8_t>>> staged;
    std::vector<std::string> opened, written;
    nvs_handle_t next_handle = 1;
    size_t commits = 0;
    esp_err_t open_error = ESP_OK, get_error = ESP_OK, set_error = ESP_OK, commit_error = ESP_OK;
};
inline State state;
}
inline esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* handle) {
    auto& s = remote_nvs_test::state;
    s.opened.emplace_back(name);
    if (s.open_error != ESP_OK) return s.open_error;
    if (mode == NVS_READONLY && !s.namespaces.count(name)) return ESP_ERR_NVS_NOT_FOUND;
    *handle = s.next_handle++;
    s.handles[*handle] = name;
    return ESP_OK;
}
inline esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out, size_t* size) {
    auto& s = remote_nvs_test::state;
    if (s.get_error != ESP_OK) return s.get_error;
    const auto& values = s.namespaces[s.handles.at(handle)];
    const auto found = values.find(key);
    if (found == values.end()) return ESP_ERR_NVS_NOT_FOUND;
    const auto supplied = *size;
    *size = found->second.size();
    if (supplied < *size) return ESP_ERR_NVS_INVALID_LENGTH;
    std::copy(found->second.begin(), found->second.end(), static_cast<uint8_t*>(out));
    return ESP_OK;
}
inline esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* data, size_t size) {
    auto& s = remote_nvs_test::state;
    if (s.set_error != ESP_OK) return s.set_error;
    s.written.push_back(s.handles.at(handle) + "/" + key);
    const auto* bytes = static_cast<const uint8_t*>(data);
    s.staged[handle][key] = std::vector<uint8_t>(bytes, bytes + size);
    return ESP_OK;
}
inline esp_err_t nvs_commit(nvs_handle_t handle) {
    auto& s = remote_nvs_test::state;
    ++s.commits;
    if (s.commit_error != ESP_OK) return s.commit_error;
    for (const auto& pair : s.staged[handle]) s.namespaces[s.handles.at(handle)][pair.first] = pair.second;
    s.staged.erase(handle);
    return ESP_OK;
}
inline void nvs_close(nvs_handle_t handle) {
    auto& s = remote_nvs_test::state;
    s.handles.erase(handle);
    s.staged.erase(handle);
}
