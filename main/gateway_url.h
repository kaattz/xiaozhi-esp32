#ifndef GATEWAY_URL_H
#define GATEWAY_URL_H

#include "settings.h"
#include "sdkconfig.h"

#include <cctype>
#include <string>

namespace gateway_url {
constexpr const char* kWakeArbitrationGatewayUrlKey = "wake_arb_url";

inline void TrimGatewayUrl(std::string& value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
}

inline std::string GetWakeArbitrationGatewayUrl() {
    Settings settings("wifi", false);
    std::string gateway_url = settings.GetString(kWakeArbitrationGatewayUrlKey);
    TrimGatewayUrl(gateway_url);

    if (gateway_url.empty()) {
        gateway_url = CONFIG_WAKE_ARBITRATION_GATEWAY_URL;
        TrimGatewayUrl(gateway_url);
    }

    while (!gateway_url.empty() && gateway_url.back() == '/') {
        gateway_url.pop_back();
    }
    return gateway_url;
}
}

#endif
