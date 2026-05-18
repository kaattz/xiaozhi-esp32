from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_ha_playback_settings_files_are_registered():
    cmake = read("main/CMakeLists.txt")

    assert '"ha_playback_settings.cc"' in cmake
    assert (ROOT / "main/ha_playback_settings.h").exists()
    assert (ROOT / "main/ha_playback_settings.cc").exists()


def test_ha_playback_settings_defaults_and_validation_are_strict():
    header = read("main/ha_playback_settings.h")
    source = read("main/ha_playback_settings.cc")

    assert "enum class TtsOutputMode" in header
    assert "kLocal" in header
    assert "kHaMediaPlayer" in header
    assert 'constexpr const char* kHaPlaybackNamespace = "ha_playback"' in source
    assert 'tts_output = "local"' in header
    assert "media_player_entity_id" in header
    assert "timeout_ms = 60000" in header
    assert "restore_listening = true" in header
    assert 'barge_in_mode = "wake_word_only"' in header
    assert 'stream_format = "ogg_opus"' in header
    assert "initial_buffer_ms = 500" in header
    assert "local_volume_when_ha_output = 0" in header

    assert "bool IsValid() const" in header
    assert "TtsOutputMode GetTtsOutputMode() const" in header
    assert 'tts_output == "local"' in source
    assert 'tts_output == "ha_media_player"' in source
    assert "!media_player_entity_id.empty()" in source
    assert "timeout_ms >= 10000" in source
    assert "timeout_ms <= 120000" in source
    assert 'barge_in_mode == "wake_word_only"' in source
    assert 'stream_format == "ogg_opus"' in source
    assert "initial_buffer_ms >= 300" in source
    assert "initial_buffer_ms <= 1000" in source
    assert "local_volume_when_ha_output >= 0" in source
    assert "local_volume_when_ha_output <= 100" in source


def test_ha_playback_settings_do_not_enable_text_tts_fallback():
    source = read("main/ha_playback_settings.cc")

    assert "tts.speak" not in source
    assert "text_to_speech" not in source
    assert "mp3" not in source.lower()
    assert "aac" not in source.lower()


def test_wifi_config_backend_exposes_and_saves_ha_playback_settings():
    header = read("local_components/esp-wifi-connect/include/wifi_configuration_ap.h")
    source = read("local_components/esp-wifi-connect/wifi_configuration_ap.cc")

    assert "ha_playback_tts_output_" in header
    assert "ha_playback_media_player_entity_id_" in header
    assert "ha_playback_timeout_ms_" in header
    assert "ha_playback_restore_listening_" in header
    assert "ha_playback_local_volume_when_ha_output_" in header
    assert "void LoadHaPlaybackSettings()" in header
    assert "esp_err_t SaveHaPlaybackSettings(const cJSON* json)" in header

    assert 'constexpr const char* kHaPlaybackNamespace = "ha_playback"' in source
    assert 'cJSON_AddItemToObject(json, "ha_playback", ha_playback)' in source
    assert 'cJSON_AddStringToObject(ha_playback, "tts_output"' in source
    assert 'cJSON_AddStringToObject(ha_playback, "media_player_entity_id"' in source
    assert 'cJSON_AddNumberToObject(ha_playback, "timeout_ms"' in source
    assert 'cJSON_AddBoolToObject(ha_playback, "restore_listening"' in source
    assert 'cJSON_AddNumberToObject(ha_playback, "local_volume_when_ha_output"' in source
    assert "LoadHaPlaybackSettings();" in source
    assert "SaveHaPlaybackSettings(json)" in source
    assert 'nvs_open(kHaPlaybackNamespace, NVS_READWRITE' in source
    assert 'tts_output == "local"' in source
    assert 'tts_output == "ha_media_player"' in source
    assert "Invalid HA playback configuration" in source


def test_wifi_config_ui_contains_ha_playback_controls():
    html = read("local_components/esp-wifi-connect/assets/wifi_configuration.html")

    assert "ha_playback_title" in html
    assert 'id="tts_output"' in html
    assert 'value="local"' in html
    assert 'value="ha_media_player"' in html
    assert 'id="ha_media_player_entity_id"' in html
    assert 'id="ha_playback_timeout_ms"' in html
    assert 'id="restore_listening_after_playback"' in html
    assert 'id="local_volume_when_ha_output"' in html
    assert "ha_playback:" in html
    assert "document.getElementById('tts_output').value" in html
    assert "document.getElementById('ha_media_player_entity_id').value.trim()" in html
    assert "data.ha_playback" in html
    assert "uses the configured gateway URL" in html or "复用当前 gateway" in html


def test_ha_playback_client_files_are_registered():
    cmake = read("main/CMakeLists.txt")

    assert '"ha_playback_client.cc"' in cmake
    assert (ROOT / "main/ha_playback_client.h").exists()
    assert (ROOT / "main/ha_playback_client.cc").exists()


def test_ha_playback_client_streams_raw_opus_to_gateway():
    header = read("main/ha_playback_client.h")
    source = read("main/ha_playback_client.cc")

    assert "class HaPlaybackClient" in header
    assert "enum class HaPlaybackResult" in header
    assert "CreateSession(const HaPlaybackSettings& settings, int sample_rate, int frame_duration_ms)" in header
    assert "StartUpload()" in header
    assert "SendFrame(const std::vector<uint8_t>& payload)" in header
    assert "Finish()" in header
    assert "Cancel()" in header
    assert "WaitForResult(int timeout_ms)" in header

    assert "gateway_url::GetWakeArbitrationGatewayUrl()" in source
    assert '"/playback/sessions"' in source
    assert '"/upload"' in source
    assert 'http->SetHeader("Content-Type", "application/json")' in source
    assert 'http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str())' in source
    assert 'http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str())' in source
    assert "CreateWebSocket" in source
    assert "CreateWebSocket(3)" in source
    assert "websocket_->Send(payload.data(), payload.size(), true)" in source
    assert "ha_playback_started" in source
    assert "ha_playback_finished" in source
    assert "ha_playback_failed" in source
    assert "superseded" in source
    assert "frame_count_" in source
    assert "audio_ms_" in source


def test_ha_playback_client_does_not_text_tts_or_transcode():
    source = read("main/ha_playback_client.cc")

    assert "tts.speak" not in source
    assert "text_to_speech" not in source
    assert "mp3" not in source.lower()
    assert "aac" not in source.lower()
    assert "transcode" not in source.lower()


def test_ha_playback_client_closes_websocket_on_timeout():
    source = read("main/ha_playback_client.cc")
    wait_for_result = source[
        source.index("HaPlaybackResult HaPlaybackClient::WaitForResult") :
        source.index("std::string HaPlaybackClient::BuildEndpointUrl")
    ]
    disconnect_handler = source[
        source.index("websocket_->OnDisconnected") :
        source.index("ESP_LOGI(TAG, \"Connecting HA playback websocket")
    ]

    assert "SetResult(HaPlaybackResult::kTimeout)" in wait_for_result
    assert "websocket_->Close()" in wait_for_result
    assert "result_ != HaPlaybackResult::kTimeout" in disconnect_handler


def test_application_has_ha_playback_state_and_loads_runtime_settings():
    header = read("main/application.h")
    source = read("main/application.cc")
    load_runtime = source[
        source.index("void Application::LoadRuntimeSettings()") :
        source.index("bool Application::IsAutoFirmwareUpgradeEnabled()")
    ]

    assert '#include "ha_playback_settings.h"' in header
    assert '#include "ha_playback_client.h"' in header
    assert "HaPlaybackSettings ha_playback_settings_" in header
    assert "std::shared_ptr<HaPlaybackClient> ha_playback_client_" in header
    assert "std::deque<PendingHaPlaybackPacket> ha_playback_pending_packets_" in header
    assert "bool ha_playback_active_" in header
    assert "bool ha_local_volume_overridden_" in header
    assert "bool IsHaPlaybackMode() const" in header
    assert "bool StartHaPlaybackSession(int sample_rate, int frame_duration_ms)" in header
    assert "void HandleIncomingHaPlaybackAudio(std::vector<uint8_t>&& payload" in header
    assert "void FinishHaPlaybackSession()" in header
    assert "void CompleteHaPlaybackSession(" in header
    assert "void CancelHaPlaybackSession()" in header

    assert "ha_playback_settings_ = HaPlaybackSettings::Load()" in load_runtime


def test_application_routes_tts_audio_to_gateway_without_local_decode_queue_in_ha_mode():
    source = read("main/application.cc")
    incoming_audio = source[
        source.index("protocol_->OnIncomingAudio") :
        source.index("protocol_->OnAudioChannelOpened")
    ]
    ha_handler = source[
        source.index("void Application::HandleIncomingHaPlaybackAudio") :
        source.index("void Application::FinishHaPlaybackSession")
    ]

    assert "IsHaPlaybackMode()" in incoming_audio
    assert "HandleIncomingHaPlaybackAudio(" in incoming_audio
    assert "audio_service_.PushPacketToDecodeQueue(std::move(packet))" in incoming_audio
    assert incoming_audio.index("HandleIncomingHaPlaybackAudio(") < incoming_audio.index("audio_service_.PushPacketToDecodeQueue(std::move(packet))")

    assert "ha_playback_client_->SendFrame(payload)" in ha_handler
    assert "ha_playback_pending_packets_.push_back" in ha_handler
    assert "PushPacketToDecodeQueue" not in ha_handler
    assert "tts.speak" not in ha_handler


def test_application_tts_start_stop_use_gateway_lifecycle_in_ha_mode():
    source = read("main/application.cc")
    tts_start_handler = source[
        source.index('strcmp(state->valuestring, "start") == 0') :
        source.index('strcmp(state->valuestring, "stop") == 0')
    ]
    tts_stop_handler = source[
        source.index('strcmp(state->valuestring, "stop") == 0') :
        source.index('strcmp(state->valuestring, "sentence_start") == 0')
    ]
    complete_handler = source[
        source.index("Application::CompleteHaPlaybackSession") :
        source.index("void Application::CancelHaPlaybackSession")
    ]

    assert "ha_playback_settings_ = HaPlaybackSettings::Load()" in tts_start_handler
    assert "IsHaPlaybackMode()" in tts_start_handler
    assert "CancelHaPlaybackSession()" in tts_start_handler
    assert "StartHaPlaybackSession(" in tts_start_handler
    assert "SetDeviceState(kDeviceStateSpeaking)" in tts_start_handler

    assert "IsHaPlaybackMode()" in tts_stop_handler
    assert "FinishHaPlaybackSession()" in tts_stop_handler
    assert "WaitForResult" not in tts_stop_handler
    assert "HaPlaybackResult::kFinished" in complete_handler
    assert "audio_service_.ResetVoiceProcessor()" in complete_handler
    assert "audio_service_.ClearUploadQueues()" in complete_handler
    assert "SetDeviceState(kDeviceStateListening)" in complete_handler
    assert "SetDeviceState(kDeviceStateIdle)" in complete_handler


def test_application_ha_playback_finish_waits_off_main_event_loop():
    source = read("main/application.cc")
    assert "void Application::FinishHaPlaybackSession" in source
    finish_handler = source[
        source.index("Application::FinishHaPlaybackSession") :
        source.index("void Application::CancelHaPlaybackSession")
    ]

    assert "xTaskCreate" in finish_handler
    assert "WaitForResult" in finish_handler
    assert "app->Schedule([app, session_id" in finish_handler
    assert "CompleteHaPlaybackSession(" in finish_handler


def test_application_ha_playback_speaking_blocks_asr_upload_and_cancels_on_barge_in():
    source = read("main/application.cc")
    send_audio_handler = source[
        source.index("if (bits & MAIN_EVENT_SEND_AUDIO)") :
        source.index("if (bits & MAIN_EVENT_WAKE_WORD_DETECTED)")
    ]
    abort_speaking = source[
        source.index("void Application::AbortSpeaking") :
        source.index("void Application::SetListeningMode")
    ]

    assert "!ha_playback_active_" in send_audio_handler
    assert "state == kDeviceStateSpeaking && board.ShouldUploadAudioDuringSpeaking() && !ha_playback_active_" in send_audio_handler
    assert "CancelHaPlaybackSession()" in abort_speaking
