#ifndef ANNOUNCEMENT_AUDIO_CLIENT_H
#define ANNOUNCEMENT_AUDIO_CLIENT_H

#include <cstdint>
#include <string>
#include <vector>

enum AnnouncementMode : int;

struct AnnouncementAudioFrames {
    int sample_rate = 0;
    int frame_duration_ms = 0;
    bool listen_after_playback = false;
    int listen_timeout_seconds = 0;
    std::vector<std::vector<uint8_t>> frames;
};

class AnnouncementAudioClient {
public:
    bool FetchFrames(const std::string& text, AnnouncementMode mode, AnnouncementAudioFrames& out);

private:
    std::string BuildEndpointUrl(const std::string& path) const;
    std::string BuildCreateJobPayload(const std::string& text, AnnouncementMode mode) const;
};

#endif // ANNOUNCEMENT_AUDIO_CLIENT_H
