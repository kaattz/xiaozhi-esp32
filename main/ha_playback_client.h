#ifndef HA_PLAYBACK_CLIENT_H
#define HA_PLAYBACK_CLIENT_H

#include "ha_playback_settings.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <web_socket.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class HaPlaybackResult {
    kNone,
    kStarted,
    kFinished,
    kFailed,
    kTimeout,
    kCancelled,
};

class HaPlaybackClient {
public:
    HaPlaybackClient();
    ~HaPlaybackClient();

    bool CreateSession(const HaPlaybackSettings& settings, int sample_rate, int frame_duration_ms);
    bool StartUpload();
    bool SendFrame(const std::vector<uint8_t>& payload);
    bool Finish();
    bool Cancel();
    HaPlaybackResult WaitForResult(int timeout_ms);

    const std::string& session_id() const { return session_id_; }
    const std::string& stream_url() const { return stream_url_; }
    uint32_t frame_count() const { return frame_count_; }
    uint32_t audio_ms() const { return audio_ms_; }
    HaPlaybackResult result() const { return result_; }

private:
    std::string BuildEndpointUrl(const std::string& path) const;
    std::string BuildUploadUrl() const;
    std::string BuildCreateSessionPayload(
        const HaPlaybackSettings& settings,
        int sample_rate,
        int frame_duration_ms) const;
    bool ParseCreateSessionResponse(const std::string& response_body);
    bool HandleStatusJson(const char* data, size_t len);
    bool SendControlMessage(const char* type);
    void SetResult(HaPlaybackResult result);

    EventGroupHandle_t event_group_ = nullptr;
    std::unique_ptr<WebSocket> websocket_;
    std::string session_id_;
    std::string upload_url_;
    std::string stream_url_;
    int frame_duration_ms_ = 0;
    uint32_t frame_count_ = 0;
    uint32_t audio_ms_ = 0;
    HaPlaybackResult result_ = HaPlaybackResult::kNone;
};

#endif // HA_PLAYBACK_CLIENT_H
