#pragma once
#include "SharedWifiConfig.h"
#include <esp_err.h>

namespace FactoryWifi {
// Returns credentials only to local connection code. Diagnostics expose status,
// never credential values. Legacy settings are read only as a migration source.
bool load(SharedWifiConfig::Credentials& credentials);
esp_err_t save(const char* ssid, const char* password);
bool hasSharedConfig();
const char* lastReason();
}
