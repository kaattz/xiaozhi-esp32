#ifndef HA_PLAYBACK_SETTINGS_H
#define HA_PLAYBACK_SETTINGS_H

#include <string>

enum class TtsOutputMode {
    kLocal,
    kHaMediaPlayer,
};

class HaPlaybackSettings {
public:
    std::string tts_output = "local";
    std::string media_player_entity_id;
    int timeout_ms = 60000;
    bool restore_listening = true;
    std::string barge_in_mode = "wake_word_only";
    std::string stream_format = "ogg_opus";
    int initial_buffer_ms = 500;
    int local_volume_when_ha_output = 0;

    bool IsValid() const;
    TtsOutputMode GetTtsOutputMode() const;

    static HaPlaybackSettings Load();
};

#endif // HA_PLAYBACK_SETTINGS_H
