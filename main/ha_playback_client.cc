#include "ha_playback_client.h"

#include "board.h"
#include "gateway_url.h"
#include "system_info.h"

#include <cJSON.h>
#include <esp_log.h>

#include <cstring>

#define TAG "HaPlaybackClient"

namespace {
constexpr EventBits_t HA_PLAYBACK_STARTED = BIT0;
constexpr EventBits_t HA_PLAYBACK_FINISHED = BIT1;
constexpr EventBits_t HA_PLAYBACK_FAILED = BIT2;

std::string ToWebSocketUrl(std::string url) {
    if (url.rfind("http://", 0) == 0) {
        url.replace(0, strlen("http://"), "ws://");
    } else if (url.rfind("https://", 0) == 0) {
        url.replace(0, strlen("https://"), "wss://");
    }
    return url;
}

bool GetStringField(cJSON* root, const char* key, std::string& out) {
    auto item = cJSON_GetObjectItem(root, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return false;
    }
    out = item->valuestring;
    return !out.empty();
}
} // namespace

HaPlaybackClient::HaPlaybackClient() {
    event_group_ = xEventGroupCreate();
}

HaPlaybackClient::~HaPlaybackClient() {
    if (websocket_ != nullptr) {
        websocket_->Close();
    }
    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
    }
}

bool HaPlaybackClient::CreateSession(
    const HaPlaybackSettings& settings,
    int sample_rate,
    int frame_duration_ms) {
    if (!settings.IsValid() || settings.GetTtsOutputMode() != TtsOutputMode::kHaMediaPlayer) {
        ESP_LOGE(TAG, "Invalid HA playback settings");
        return false;
    }

    auto url = BuildEndpointUrl("/playback/sessions");
    if (url.empty()) {
        ESP_LOGE(TAG, "HA playback gateway URL is empty");
        return false;
    }

    session_id_.clear();
    upload_url_.clear();
    stream_url_.clear();
    frame_duration_ms_ = frame_duration_ms;
    frame_count_ = 0;
    audio_ms_ = 0;
    result_ = HaPlaybackResult::kNone;
    xEventGroupClearBits(event_group_, HA_PLAYBACK_STARTED | HA_PLAYBACK_FINISHED | HA_PLAYBACK_FAILED);

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    http->SetTimeout(settings.timeout_ms);
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    http->SetContent(BuildCreateSessionPayload(settings, sample_rate, frame_duration_ms));

    if (!http->Open("POST", url)) {
        ESP_LOGE(TAG, "Failed to create HA playback session: %s", url.c_str());
        return false;
    }

    auto status_code = http->GetStatusCode();
    auto response_body = http->ReadAll();
    http->Close();
    if (status_code != 200) {
        ESP_LOGE(TAG, "HA playback session failed, status=%d, body=%s", status_code, response_body.c_str());
        return false;
    }

    if (!ParseCreateSessionResponse(response_body)) {
        ESP_LOGE(TAG, "HA playback session response is invalid");
        return false;
    }

    ESP_LOGI(TAG, "HA playback session created: %s", session_id_.c_str());
    return true;
}

bool HaPlaybackClient::StartUpload() {
    if (session_id_.empty()) {
        ESP_LOGE(TAG, "Cannot start HA playback upload without session");
        return false;
    }

    auto url = upload_url_.empty() ? BuildUploadUrl() : ToWebSocketUrl(upload_url_);
    if (url.empty()) {
        ESP_LOGE(TAG, "HA playback upload URL is empty");
        return false;
    }

    auto network = Board::GetInstance().GetNetwork();
    websocket_ = network->CreateWebSocket(3);
    if (websocket_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create HA playback websocket");
        return false;
    }

    websocket_->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    websocket_->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        if (!binary) {
            HandleStatusJson(data, len);
        }
    });
    websocket_->OnDisconnected([this]() {
        if (result_ != HaPlaybackResult::kFinished &&
            result_ != HaPlaybackResult::kFailed &&
            result_ != HaPlaybackResult::kTimeout &&
            result_ != HaPlaybackResult::kCancelled) {
            ESP_LOGW(TAG, "HA playback websocket disconnected before terminal state");
            SetResult(HaPlaybackResult::kFailed);
        }
    });

    ESP_LOGI(TAG, "Connecting HA playback websocket: %s", url.c_str());
    if (!websocket_->Connect(url.c_str())) {
        ESP_LOGE(TAG, "Failed to connect HA playback websocket, code=%d", websocket_->GetLastError());
        websocket_.reset();
        return false;
    }
    return true;
}

bool HaPlaybackClient::SendFrame(const std::vector<uint8_t>& payload) {
    if (payload.empty()) {
        return true;
    }
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        ESP_LOGE(TAG, "HA playback websocket is not connected");
        return false;
    }

    if (!websocket_->Send(payload.data(), payload.size(), true)) {
        ESP_LOGE(TAG, "Failed to send HA playback opus frame");
        SetResult(HaPlaybackResult::kFailed);
        return false;
    }

    ++frame_count_;
    audio_ms_ += frame_duration_ms_ > 0 ? frame_duration_ms_ : 0;
    return true;
}

bool HaPlaybackClient::Finish() {
    if (!SendControlMessage("end")) {
        return false;
    }
    ESP_LOGI(TAG, "HA playback upload finished, frames=%u, audio_ms=%u",
             static_cast<unsigned>(frame_count_),
             static_cast<unsigned>(audio_ms_));
    return true;
}

bool HaPlaybackClient::Cancel() {
    if (websocket_ != nullptr && websocket_->IsConnected()) {
        SendControlMessage("cancel");
    }
    SetResult(HaPlaybackResult::kCancelled);
    if (websocket_ != nullptr) {
        websocket_->Close();
    }
    ESP_LOGI(TAG, "HA playback cancelled, session=%s", session_id_.c_str());
    return true;
}

HaPlaybackResult HaPlaybackClient::WaitForResult(int timeout_ms) {
    EventBits_t bits = xEventGroupWaitBits(
        event_group_,
        HA_PLAYBACK_FINISHED | HA_PLAYBACK_FAILED,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if (bits & HA_PLAYBACK_FINISHED) {
        return HaPlaybackResult::kFinished;
    }
    if (bits & HA_PLAYBACK_FAILED) {
        return result_;
    }

    ESP_LOGW(TAG, "HA playback timeout, session=%s, frames=%u, audio_ms=%u",
             session_id_.c_str(),
             static_cast<unsigned>(frame_count_),
             static_cast<unsigned>(audio_ms_));
    SetResult(HaPlaybackResult::kTimeout);
    if (websocket_ != nullptr) {
        websocket_->Close();
    }
    return HaPlaybackResult::kTimeout;
}

std::string HaPlaybackClient::BuildEndpointUrl(const std::string& path) const {
    auto gateway_url = gateway_url::GetWakeArbitrationGatewayUrl();
    if (gateway_url.empty()) {
        return "";
    }
    return gateway_url + path;
}

std::string HaPlaybackClient::BuildUploadUrl() const {
    return ToWebSocketUrl(BuildEndpointUrl("/playback/sessions/" + session_id_ + "/upload"));
}

std::string HaPlaybackClient::BuildCreateSessionPayload(
    const HaPlaybackSettings& settings,
    int sample_rate,
    int frame_duration_ms) const {
    cJSON* payload = cJSON_CreateObject();
    if (payload == nullptr) {
        return "{}";
    }

    cJSON_AddStringToObject(payload, "device_id", SystemInfo::GetMacAddress().c_str());
    cJSON_AddStringToObject(payload, "client_id", Board::GetInstance().GetUuid().c_str());
    cJSON_AddStringToObject(payload, "media_player_entity_id", settings.media_player_entity_id.c_str());
    cJSON_AddStringToObject(payload, "stream_format", settings.stream_format.c_str());
    cJSON_AddNumberToObject(payload, "sample_rate", sample_rate);
    cJSON_AddNumberToObject(payload, "frame_duration_ms", frame_duration_ms);
    cJSON_AddNumberToObject(payload, "initial_buffer_ms", settings.initial_buffer_ms);
    cJSON_AddNumberToObject(payload, "timeout_ms", settings.timeout_ms);
    cJSON_AddBoolToObject(payload, "restore_listening", settings.restore_listening);
    cJSON_AddBoolToObject(payload, "replace_existing", true);

    auto json_str = cJSON_PrintUnformatted(payload);
    std::string json = "{}";
    if (json_str != nullptr) {
        json = json_str;
        cJSON_free(json_str);
    }
    cJSON_Delete(payload);
    return json;
}

bool HaPlaybackClient::ParseCreateSessionResponse(const std::string& response_body) {
    cJSON* root = cJSON_Parse(response_body.c_str());
    if (root == nullptr) {
        return false;
    }

    const bool ok =
        GetStringField(root, "session_id", session_id_) &&
        GetStringField(root, "stream_url", stream_url_);
    GetStringField(root, "upload_url", upload_url_);

    cJSON_Delete(root);
    return ok && !session_id_.empty();
}

bool HaPlaybackClient::HandleStatusJson(const char* data, size_t len) {
    cJSON* root = cJSON_ParseWithLength(data, len);
    if (root == nullptr) {
        return false;
    }

    auto type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type) && type->valuestring != nullptr) {
        if (strcmp(type->valuestring, "ha_playback_started") == 0) {
            ESP_LOGI(TAG, "HA playback started, session=%s", session_id_.c_str());
            SetResult(HaPlaybackResult::kStarted);
            xEventGroupSetBits(event_group_, HA_PLAYBACK_STARTED);
        } else if (strcmp(type->valuestring, "ha_playback_finished") == 0) {
            ESP_LOGI(TAG, "HA playback finished, session=%s, frames=%u, audio_ms=%u",
                     session_id_.c_str(),
                     static_cast<unsigned>(frame_count_),
                     static_cast<unsigned>(audio_ms_));
            SetResult(HaPlaybackResult::kFinished);
        } else if (strcmp(type->valuestring, "ha_playback_failed") == 0) {
            auto reason = cJSON_GetObjectItem(root, "reason");
            if (cJSON_IsString(reason) && reason->valuestring != nullptr &&
                strcmp(reason->valuestring, "superseded") == 0) {
                ESP_LOGW(TAG, "HA playback superseded, session=%s", session_id_.c_str());
            } else {
                ESP_LOGE(TAG, "HA playback failed, session=%s", session_id_.c_str());
            }
            SetResult(HaPlaybackResult::kFailed);
        }
    }

    cJSON_Delete(root);
    return true;
}

bool HaPlaybackClient::SendControlMessage(const char* type) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }

    cJSON* payload = cJSON_CreateObject();
    if (payload == nullptr) {
        return false;
    }
    cJSON_AddStringToObject(payload, "type", type);

    auto json_str = cJSON_PrintUnformatted(payload);
    std::string json = "{}";
    if (json_str != nullptr) {
        json = json_str;
        cJSON_free(json_str);
    }
    cJSON_Delete(payload);
    return websocket_->Send(json);
}

void HaPlaybackClient::SetResult(HaPlaybackResult result) {
    result_ = result;
    if (result == HaPlaybackResult::kFinished) {
        xEventGroupSetBits(event_group_, HA_PLAYBACK_FINISHED);
    } else if (result == HaPlaybackResult::kFailed ||
               result == HaPlaybackResult::kTimeout ||
               result == HaPlaybackResult::kCancelled) {
        xEventGroupSetBits(event_group_, HA_PLAYBACK_FAILED);
    }
}
