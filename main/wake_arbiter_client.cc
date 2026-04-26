#include "wake_arbiter_client.h"
#include "board.h"
#include "system_info.h"

#include <cJSON.h>
#include <esp_log.h>

#include <cstring>

#define TAG "WakeArbiterClient"

namespace {
constexpr int kWakeArbitrationTimeoutSeconds = 2;
}

WakeArbitrationDecision ParseWakeArbitrationDecision(const std::string& response_body) {
    cJSON* root = cJSON_Parse(response_body.c_str());
    if (root == nullptr) {
        return WakeArbitrationDecision::kDenySession;
    }

    auto type = cJSON_GetObjectItem(root, "type");
    auto decision = WakeArbitrationDecision::kDenySession;
    if (cJSON_IsString(type) && strcmp(type->valuestring, "allow_session") == 0) {
        decision = WakeArbitrationDecision::kAllowSession;
    }

    cJSON_Delete(root);
    return decision;
}

bool WakeArbiterClient::RequestSession(const std::string& wake_word) {
    auto url = BuildEndpointUrl("/wake-detected");
    if (url.empty()) {
        ESP_LOGE(TAG, "Wake arbitration gateway URL is empty");
        return false;
    }

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(kWakeArbitrationTimeoutSeconds);
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid());
    http->SetContent(BuildWakeDetectedPayload(wake_word));

    if (!http->Open("POST", url)) {
        ESP_LOGE(TAG, "Failed to open wake arbitration request: %s", url.c_str());
        return false;
    }

    auto status_code = http->GetStatusCode();
    auto response_body = http->ReadAll();
    http->Close();

    if (status_code != 200) {
        ESP_LOGE(TAG, "Wake arbitration failed, status=%d, body=%s", status_code, response_body.c_str());
        return false;
    }

    auto decision = ParseWakeArbitrationDecision(response_body);
    if (decision == WakeArbitrationDecision::kAllowSession) {
        ESP_LOGI(TAG, "Wake arbitration allowed");
        return true;
    }

    ESP_LOGI(TAG, "Wake arbitration denied");
    return false;
}

bool WakeArbiterClient::EndSession() {
    auto url = BuildEndpointUrl("/session/end");
    if (url.empty()) {
        ESP_LOGE(TAG, "Wake arbitration gateway URL is empty");
        return false;
    }

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(kWakeArbitrationTimeoutSeconds);
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid());
    http->SetContent(BuildSessionEndPayload());

    if (!http->Open("POST", url)) {
        ESP_LOGE(TAG, "Failed to open session end request: %s", url.c_str());
        return false;
    }

    auto status_code = http->GetStatusCode();
    auto response_body = http->ReadAll();
    http->Close();

    if (status_code != 200) {
        ESP_LOGW(TAG, "Session end request failed, status=%d, body=%s", status_code, response_body.c_str());
        return false;
    }

    ESP_LOGI(TAG, "Session end notified");
    return true;
}

std::string WakeArbiterClient::BuildEndpointUrl(const std::string& path) const {
    std::string gateway_url = CONFIG_WAKE_ARBITRATION_GATEWAY_URL;
    while (!gateway_url.empty() && gateway_url.back() == '/') {
        gateway_url.pop_back();
    }
    if (gateway_url.empty()) {
        return "";
    }
    return gateway_url + path;
}

std::string WakeArbiterClient::BuildWakeDetectedPayload(const std::string& wake_word) const {
    cJSON* payload = cJSON_CreateObject();
    if (payload == nullptr) {
        return "{}";
    }

    cJSON_AddStringToObject(payload, "device_id", SystemInfo::GetMacAddress().c_str());
    cJSON_AddStringToObject(payload, "client_id", Board::GetInstance().GetUuid().c_str());
    cJSON_AddStringToObject(payload, "wake_word", wake_word.c_str());

    auto json_str = cJSON_PrintUnformatted(payload);
    std::string json = "{}";
    if (json_str != nullptr) {
        json = json_str;
        cJSON_free(json_str);
    }
    cJSON_Delete(payload);
    return json;
}

std::string WakeArbiterClient::BuildSessionEndPayload() const {
    cJSON* payload = cJSON_CreateObject();
    if (payload == nullptr) {
        return "{}";
    }

    cJSON_AddStringToObject(payload, "device_id", SystemInfo::GetMacAddress().c_str());

    auto json_str = cJSON_PrintUnformatted(payload);
    std::string json = "{}";
    if (json_str != nullptr) {
        json = json_str;
        cJSON_free(json_str);
    }
    cJSON_Delete(payload);
    return json;
}
