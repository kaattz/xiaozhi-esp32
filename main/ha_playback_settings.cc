#include "ha_playback_settings.h"

#include "settings.h"

#include <esp_log.h>

#define TAG "HaPlaybackSettings"

namespace {
constexpr const char* kHaPlaybackNamespace = "ha_playback";
}

bool HaPlaybackSettings::IsValid() const {
    const bool valid_output = tts_output == "local" || tts_output == "ha_media_player";
    const bool has_target = tts_output == "local" || !media_player_entity_id.empty();
    return valid_output &&
        has_target &&
        timeout_ms >= 10000 &&
        timeout_ms <= 120000 &&
        barge_in_mode == "wake_word_only" &&
        stream_format == "ogg_opus" &&
        initial_buffer_ms >= 300 &&
        initial_buffer_ms <= 1000 &&
        local_volume_when_ha_output >= 0 &&
        local_volume_when_ha_output <= 100;
}

TtsOutputMode HaPlaybackSettings::GetTtsOutputMode() const {
    if (tts_output == "ha_media_player") {
        return TtsOutputMode::kHaMediaPlayer;
    }
    return TtsOutputMode::kLocal;
}

HaPlaybackSettings HaPlaybackSettings::Load() {
    Settings settings(kHaPlaybackNamespace, false);

    HaPlaybackSettings config;
    config.tts_output = settings.GetString("tts_output", config.tts_output);
    config.media_player_entity_id = settings.GetString("media_player_entity_id");
    config.timeout_ms = settings.GetInt("timeout_ms", config.timeout_ms);
    config.restore_listening = settings.GetBool("restore_listening", config.restore_listening);
    config.barge_in_mode = settings.GetString("barge_in_mode", config.barge_in_mode);
    config.stream_format = settings.GetString("stream_format", config.stream_format);
    config.initial_buffer_ms = settings.GetInt("initial_buffer_ms", config.initial_buffer_ms);
    config.local_volume_when_ha_output = settings.GetInt(
        "local_volume_when_ha_output",
        config.local_volume_when_ha_output);

    if (!config.IsValid()) {
        ESP_LOGW(TAG, "HA playback settings are invalid");
    }
    return config;
}
