#include "ha_mqtt_settings.h"

#include "settings.h"
#include "system_info.h"

#include <algorithm>
#include <cctype>

#include <esp_log.h>

#define TAG "HaMqttSettings"

namespace {
constexpr const char* kHaMqttNamespace = "ha_mqtt";

std::string SanitizeClientId(std::string value) {
    for (char& ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        if (!std::isalnum(c) && ch != '_') {
            ch = '_';
        }
    }
    value.erase(std::remove(value.begin(), value.end(), '\0'), value.end());
    return value;
}

std::string GenerateDefaultClientId() {
    auto uuid = SystemInfo::GetMacAddress();
    if (uuid.empty()) {
        uuid = BOARD_TYPE;
    }
    return SanitizeClientId("xiaozhi_" + uuid);
}
} // namespace

bool HaMqttSettings::IsValid() const {
    return enabled && !host.empty() && port > 0 && port <= 65535 && !client_id.empty() && !device_name.empty();
}

HaMqttSettings HaMqttSettings::Load() {
    Settings settings("ha_mqtt", false);

    HaMqttSettings config;
    config.enabled = settings.GetBool("enabled", false);
    config.host = settings.GetString("host");
    config.port = settings.GetInt("port", 1883);
    config.username = settings.GetString("username");
    config.password = settings.GetString("password");
    config.client_id = SanitizeClientId(settings.GetString("client_id", GenerateDefaultClientId()));
    config.device_name = settings.GetString("device_name", BOARD_NAME);
    config.model = settings.GetString("model", "xiaozhi-esp32");
    config.manufacturer = settings.GetString("manufacturer", "xiaozhi");

    if (config.client_id.empty()) {
        config.client_id = GenerateDefaultClientId();
    }
    if (config.device_name.empty()) {
        config.device_name = BOARD_NAME;
    }

    if (config.enabled && !config.IsValid()) {
        ESP_LOGW(TAG, "HA MQTT is enabled but settings are incomplete");
    }

    return config;
}
