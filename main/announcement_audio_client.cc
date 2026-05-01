#include "announcement_audio_client.h"

#include "application.h"
#include "board.h"
#include "system_info.h"

#include <cJSON.h>
#include <esp_log.h>
#include <mbedtls/base64.h>

#include <cstring>
#include <utility>

#define TAG "AnnouncementAudioClient"

namespace {
constexpr int kAnnouncementTimeoutMs = 10000;
constexpr int kAnnouncementFramesPageLimit = 4;
constexpr int kAnnouncementMaxFramePages = 64;

bool DecodeBase64Frame(const char* encoded, std::vector<uint8_t>& decoded) {
    size_t decoded_len = 0;
    auto ret = mbedtls_base64_decode(
        nullptr,
        0,
        &decoded_len,
        reinterpret_cast<const unsigned char*>(encoded),
        strlen(encoded));
    if (ret != 0 && ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        return false;
    }

    decoded.resize(decoded_len);
    ret = mbedtls_base64_decode(
        decoded.data(),
        decoded.size(),
        &decoded_len,
        reinterpret_cast<const unsigned char*>(encoded),
        strlen(encoded));
    if (ret != 0) {
        return false;
    }

    decoded.resize(decoded_len);
    return !decoded.empty();
}

bool ParseCreateJobResponse(
    const std::string& response_body,
    std::string& job_id,
    AnnouncementAudioFrames& out) {
    cJSON* root = cJSON_Parse(response_body.c_str());
    if (root == nullptr) {
        return false;
    }

    auto job_id_json = cJSON_GetObjectItem(root, "job_id");
    auto ok = cJSON_IsString(job_id_json) && job_id_json->valuestring != nullptr;
    if (ok) {
        job_id = job_id_json->valuestring;
    }

    cJSON_Delete(root);
    return ok && !job_id.empty();
}

bool ParseFramesResponse(
    const std::string& response_body,
    AnnouncementAudioFrames& out,
    int& next_offset) {
    cJSON* root = cJSON_Parse(response_body.c_str());
    if (root == nullptr) {
        return false;
    }

    auto sample_rate = cJSON_GetObjectItem(root, "sample_rate");
    auto frame_duration_ms = cJSON_GetObjectItem(root, "frame_duration_ms");
    auto frames_base64 = cJSON_GetObjectItem(root, "frames_base64");
    auto next_offset_json = cJSON_GetObjectItem(root, "next_offset");
    if (!cJSON_IsNumber(sample_rate) ||
        !cJSON_IsNumber(frame_duration_ms) ||
        !cJSON_IsArray(frames_base64)) {
        cJSON_Delete(root);
        return false;
    }

    if (out.sample_rate == 0) {
        out.sample_rate = sample_rate->valueint;
    }
    if (out.frame_duration_ms == 0) {
        out.frame_duration_ms = frame_duration_ms->valueint;
    }
    if (out.sample_rate != sample_rate->valueint ||
        out.frame_duration_ms != frame_duration_ms->valueint) {
        cJSON_Delete(root);
        return false;
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, frames_base64) {
        if (!cJSON_IsString(item) || item->valuestring == nullptr) {
            cJSON_Delete(root);
            return false;
        }
        std::vector<uint8_t> frame;
        if (!DecodeBase64Frame(item->valuestring, frame)) {
            cJSON_Delete(root);
            return false;
        }
        out.frames.push_back(std::move(frame));
    }

    next_offset = -1;
    if (cJSON_IsNumber(next_offset_json)) {
        next_offset = next_offset_json->valueint;
    }

    cJSON_Delete(root);
    if (out.sample_rate <= 0 ||
        out.frame_duration_ms <= 0 ||
        out.frames.empty()) {
        return false;
    }

    return true;
}
} // namespace

bool AnnouncementAudioClient::FetchFrames(
    const std::string& text,
    AnnouncementAudioFrames& out) {
    auto create_url = BuildEndpointUrl("/announcement/jobs");
    if (create_url.empty()) {
        ESP_LOGE(TAG, "Announcement gateway URL is empty");
        return false;
    }

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    http->SetTimeout(kAnnouncementTimeoutMs);
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    http->SetContent(BuildCreateJobPayload(text));

    if (!http->Open("POST", create_url)) {
        ESP_LOGE(TAG, "Failed to create announcement job: %s", create_url.c_str());
        return false;
    }

    auto status_code = http->GetStatusCode();
    auto response_body = http->ReadAll();
    http->Close();
    if (status_code != 200) {
        ESP_LOGE(TAG, "Announcement job failed, status=%d", status_code);
        return false;
    }

    std::string job_id;
    if (!ParseCreateJobResponse(response_body, job_id, out)) {
        ESP_LOGE(TAG, "Announcement job response is invalid");
        return false;
    }
    ESP_LOGI(TAG, "Created announcement job: %s", job_id.c_str());

    int offset = 0;
    bool frames_complete = false;
    for (int page = 0; page < kAnnouncementMaxFramePages; ++page) {
        auto frames_url = BuildEndpointUrl(
            "/announcement/jobs/" + job_id + "/frames?offset=" +
            std::to_string(offset) + "&limit=" +
            std::to_string(kAnnouncementFramesPageLimit));
        http = network->CreateHttp(0);
        http->SetTimeout(kAnnouncementTimeoutMs);
        http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
        http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
        if (!http->Open("GET", frames_url)) {
            ESP_LOGE(TAG, "Failed to fetch announcement frames: %s", frames_url.c_str());
            return false;
        }

        status_code = http->GetStatusCode();
        response_body = http->ReadAll();
        http->Close();
        if (status_code != 200) {
            ESP_LOGE(TAG, "Announcement frames failed, status=%d", status_code);
            return false;
        }

        int next_offset = -1;
        auto frames_before = out.frames.size();
        if (!ParseFramesResponse(response_body, out, next_offset)) {
            ESP_LOGE(TAG, "Announcement frames response is invalid");
            return false;
        }
        ESP_LOGI(TAG, "Fetched announcement frame page offset=%d count=%u",
                 offset,
                 static_cast<unsigned>(out.frames.size() - frames_before));

        if (next_offset < 0) {
            frames_complete = true;
            break;
        }
        if (next_offset <= offset) {
            ESP_LOGE(TAG, "Announcement frames next_offset did not advance: %d", next_offset);
            return false;
        }
        offset = next_offset;
    }
    if (!frames_complete) {
        ESP_LOGE(TAG, "Announcement frame pagination exceeded %d pages", kAnnouncementMaxFramePages);
        return false;
    }

    ESP_LOGI(TAG, "Fetched %u announcement frames", static_cast<unsigned>(out.frames.size()));
    return true;
}

std::string AnnouncementAudioClient::BuildEndpointUrl(const std::string& path) const {
    std::string gateway_url = CONFIG_WAKE_ARBITRATION_GATEWAY_URL;
    while (!gateway_url.empty() && gateway_url.back() == '/') {
        gateway_url.pop_back();
    }
    if (gateway_url.empty()) {
        return "";
    }
    return gateway_url + path;
}

std::string AnnouncementAudioClient::BuildCreateJobPayload(
    const std::string& text) const {
    cJSON* payload = cJSON_CreateObject();
    if (payload == nullptr) {
        return "{}";
    }
    cJSON_AddStringToObject(payload, "device_id", SystemInfo::GetMacAddress().c_str());
    cJSON_AddStringToObject(payload, "client_id", Board::GetInstance().GetUuid().c_str());
    cJSON_AddStringToObject(payload, "text", text.c_str());

    auto json_str = cJSON_PrintUnformatted(payload);
    std::string json = "{}";
    if (json_str != nullptr) {
        json = json_str;
        cJSON_free(json_str);
    }
    cJSON_Delete(payload);
    return json;
}
