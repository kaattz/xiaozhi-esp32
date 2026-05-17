from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_voice_pe_xmos_helper_exposes_fixed_i2c_protocol():
    header = read("main/boards/home-assistant-voice-pe/voice_pe_xmos.h")
    source = read("main/boards/home-assistant-voice-pe/voice_pe_xmos.cc")

    assert "class VoicePeXmos" in header
    assert "Initialize()" in header
    assert "ReadVersion" in header
    assert "WritePipelineStages" in header
    assert "0x42" in source
    assert "240" in source
    assert "88" in source
    assert "0x80" in source
    assert "pdMS_TO_TICKS(250)" in source
    assert "pdMS_TO_TICKS(4000)" in source


def test_voice_pe_aic3204_driver_uses_fixed_esphome_register_sequence():
    header = read("main/boards/home-assistant-voice-pe/aic3204_audio_dac.h")
    source = read("main/boards/home-assistant-voice-pe/aic3204_audio_dac.cc")

    assert "class Aic3204AudioDac" in header
    assert "Initialize()" in header
    assert "SetMuted" in header
    assert "SetVolume" in header
    assert "35631be260c0fd6fae1e4c945f16790979ba777c" in source
    for token in [
        "AIC3204_NDAC",
        "AIC3204_MDAC",
        "AIC3204_DOSR",
        "AIC3204_CODEC_IF",
        "AIC3204_DAC_CH_SET1",
        "AIC3204_DACL_VOL_D",
        "AIC3204_DACR_VOL_D",
        "AIC3204_DAC_CH_SET2",
    ]:
        assert token in source


def test_voice_pe_audio_codec_declares_real_sample_rates_and_pcm_conversion():
    header = read("main/boards/home-assistant-voice-pe/voice_pe_audio_codec.h")
    source = read("main/boards/home-assistant-voice-pe/voice_pe_audio_codec.cc")
    board = read("main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc")

    assert "class VoicePeAudioCodec" in header
    assert "SaturateMicSample" in header
    assert "LogMicProbe" in header
    assert "i2s_channel_init_std_mode" in source
    assert "VOICE_PE_MIC_BCLK_GPIO" in source
    assert "VOICE_PE_SPK_BCLK_GPIO" in source
    assert "kWakeWordMicSlot = 1" in source
    assert "kVoiceMicSlot = 1" in source
    assert "kMicSampleShift = 8" in source
    assert "kMicGainNumerator = 3" in source
    assert "kMicGainDenominator = 2" in source
    assert source.count("I2S_ROLE_SLAVE") == 2
    assert "I2S_ROLE_MASTER" not in source
    assert "i2s_mode: secondary" in source
    assert "kMicReadTimeoutMs = 200" in source
    assert "pdMS_TO_TICKS(200)" not in source
    assert ">> kMicSampleShift" in source
    assert "* kMicGainNumerator / kMicGainDenominator" in source
    assert "INT16_MIN" in source
    assert "INT16_MAX" in source
    assert "Aic3204AudioDac" in header
    assert "VoicePeAudioCodec audio_codec" in board
    assert "OnDoubleClick" in board
    assert "kDeviceStateWifiConfiguring" in board
    assert "PlayTestTone(1000)" in board
    assert "Voice PE audio codec is not implemented yet" not in board
    assert 'ESP_LOGI(TAG, "mic probe:' not in source
    assert 'ESP_LOGD(TAG, "mic probe:' in source
    assert "LogOutputProbe" in header
    assert "output probe:" in source
    assert "peak=%d" in source
    assert "volume=%d" in source


def test_audio_output_retries_partial_codec_writes_until_pcm_is_fully_written():
    codec_source = read("main/audio/audio_codec.cc")
    voice_pe_source = read("main/boards/home-assistant-voice-pe/voice_pe_audio_codec.cc")

    output_data = codec_source[
        codec_source.index("void AudioCodec::OutputData") :
        codec_source.index("bool AudioCodec::InputData")
    ]

    assert "int written = 0" in output_data
    assert "while (written < static_cast<int>(data.size()))" in output_data
    assert "int result = Write(data.data() + written, data.size() - written)" in output_data
    assert "written += result" in output_data
    assert "ESP_LOGE(TAG, \"Audio output write failed" in output_data
    assert "I2S TX partial write" in voice_pe_source
    assert "LogOutputProbe(data, samples);" not in voice_pe_source
    assert "LogOutputProbe(data, samples_written)" in voice_pe_source
    assert voice_pe_source.index("i2s_channel_write") < voice_pe_source.index("LogOutputProbe(data, samples_written)")


def test_voice_pe_interaction_gpio_constants_and_wake_aec_config_are_declared():
    config = read("main/boards/home-assistant-voice-pe/config.h")
    config_json = read("main/boards/home-assistant-voice-pe/config.json")
    kconfig = read("main/Kconfig.projbuild")

    for name, value in [
        ("VOICE_PE_LED_DATA_GPIO", "GPIO_NUM_21"),
        ("VOICE_PE_LED_POWER_GPIO", "GPIO_NUM_45"),
        ("VOICE_PE_LED_COUNT", "12"),
        ("VOICE_PE_MUTE_GPIO", "GPIO_NUM_3"),
        ("VOICE_PE_MUTE_ACTIVE_LEVEL", "1"),
        ("VOICE_PE_ENCODER_A_GPIO", "GPIO_NUM_16"),
        ("VOICE_PE_ENCODER_B_GPIO", "GPIO_NUM_18"),
        ("VOICE_PE_JACK_DETECT_GPIO", "GPIO_NUM_17"),
        ("VOICE_PE_JACK_INSERTED_LEVEL", "1"),
        ("VOICE_PE_VOLUME_STEP", "5"),
    ]:
        assert f"#define {name}" in config
        assert value in config

    assert "#define VOICE_PE_VOLUME_STEP            5" in config
    assert "#define AUDIO_INPUT_REFERENCE    true" in config
    assert "CONFIG_WAKE_WORD_DISABLED=y" not in config_json
    assert "CONFIG_USE_AFE_WAKE_WORD=y" in config_json
    assert "CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y" in config_json
    assert "CONFIG_SEND_WAKE_WORD_DATA=y" in config_json
    assert "CONFIG_USE_DEVICE_AEC=y" in config_json
    assert "CONFIG_USE_CUSTOM_WAKE_WORD" not in config_json
    assert "CONFIG_USE_SERVER_AEC" not in config_json
    assert "BOARD_TYPE_HOME_ASSISTANT_VOICE_PE" in kconfig
    assert "config USE_DEVICE_AEC" in kconfig
    assert "BOARD_TYPE_HOME_ASSISTANT_VOICE_PE" in kconfig[kconfig.index("config USE_DEVICE_AEC"):]


def test_voice_pe_led_status_ring_uses_existing_circular_strip():
    board = read("main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc")

    assert '#include "led/circular_strip.h"' in board
    assert "std::unique_ptr<CircularStrip> led_strip_" in board
    assert "InitializeLed()" in board
    assert "VOICE_PE_LED_POWER_GPIO" in board
    assert "gpio_set_level(VOICE_PE_LED_POWER_GPIO, 1)" in board
    assert "std::make_unique<CircularStrip>(VOICE_PE_LED_DATA_GPIO, VOICE_PE_LED_COUNT)" in board
    assert "virtual Led* GetLed() override" in board
    assert "return led_strip_.get()" in board
    assert board.index("InitializeXmos();") < board.index("InitializeLed();")
    assert board.index("InitializeLed();") < board.index("InitializeButtons();")


def test_voice_pe_led_scenes_follow_official_voice_pe_colors():
    strip_header = read("main/led/circular_strip.h")
    strip_source = read("main/led/circular_strip.cc")
    wifi_configuring = strip_source[
        strip_source.index("case kDeviceStateWifiConfiguring:") :
        strip_source.index("case kDeviceStateIdle:")
    ]
    listening = strip_source[
        strip_source.index("case kDeviceStateListening:") :
        strip_source.index("case kDeviceStateSpeaking:")
    ]
    speaking = strip_source[
        strip_source.index("case kDeviceStateSpeaking:") :
        strip_source.index("case kDeviceStateUpgrading:")
    ]

    assert "constexpr StripColor kVoicePeListeningColor = { 4, 32, 4 }" in strip_source
    assert "constexpr StripColor kVoicePeSpeakingColor = { 4, 24, 32 }" in strip_source
    assert "constexpr StripColor kVoicePeWarmWhiteColor = { 32, 28, 23 }" in strip_source
    assert "void ScrollReverse(StripColor low, StripColor high, int length, int interval_ms)" in strip_header
    assert "void CircularStrip::ScrollReverse(" in strip_source
    assert "Blink(kVoicePeWarmWhiteColor, 500)" in wifi_configuring
    assert "StripColor color = { default_brightness_, low_brightness_, low_brightness_ }" not in listening
    assert "Scroll(kVoicePeOffColor, kVoicePeListeningColor, 3, 100)" in listening
    assert "ScrollReverse(kVoicePeOffColor, kVoicePeSpeakingColor, 3, 50)" in speaking


def test_voice_pe_led_red_indicators_distinguish_mute_silent_and_error():
    strip_header = read("main/led/circular_strip.h")
    strip_source = read("main/led/circular_strip.cc")
    application_header = read("main/application.h")
    application = read("main/application.cc")
    board = read("main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc")
    codec = read("main/boards/home-assistant-voice-pe/voice_pe_audio_codec.cc")

    assert "constexpr StripColor kVoicePeRedColor = { 32, 0, 0 }" in strip_source
    assert "void ShowMutedOrSilentIndicator(bool microphone_muted, bool speaker_silent)" in strip_header
    assert "void CircularStrip::ShowMutedOrSilentIndicator(bool microphone_muted, bool speaker_silent)" in strip_source
    assert "colors[3] = kVoicePeRedColor" in strip_source
    assert "colors[9] = kVoicePeRedColor" in strip_source
    assert "colors[6] = kVoicePeRedColor" in strip_source
    assert "if (app.IsErrorAlertActive())" in strip_source
    assert "Blink(kVoicePeRedColor, 250)" in strip_source
    assert "board.IsMicrophoneMuted()" in strip_source
    assert "codec != nullptr && codec->output_volume() == 0" in strip_source
    assert "ShowMutedOrSilentIndicator(microphone_muted, speaker_silent)" in strip_source
    assert "bool IsErrorAlertActive() const" in application_header
    assert "bool error_alert_active_ = false" in application_header
    assert "error_alert_active_ = strcmp(status, Lang::Strings::ERROR) == 0" in application
    assert "error_alert_active_ = false" in application
    assert "Board::GetInstance().GetLed()->OnStateChanged()" in application
    assert "GetLed()->OnStateChanged()" in board
    assert "Board::GetInstance().GetLed()->OnStateChanged()" in codec


def test_voice_pe_mute_switch_blocks_microphone_entry_without_muting_speaker():
    board = read("main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc")
    board_header = read("main/boards/common/board.h")
    application = read("main/application.cc")

    assert "VOICE_PE_MUTE_GPIO" in board
    assert "kMutePollIntervalUs = 50 * 1000" in board
    assert "kDebounceStableSamples = 2" in board
    assert "input_poll_timer_" in board
    assert "esp_timer_start_periodic(input_poll_timer_, kMutePollIntervalUs)" in board
    assert "ReadMuteSwitch()" in board
    assert "OnMuteChanged" in board
    assert "muted_" in board
    assert "if (muted_.load())" in board
    assert "Voice PE center button clicked" in board
    assert "Application::GetInstance().Schedule([this]()" in board
    assert "app.StopListening()" in board
    assert "kDeviceStateConnecting" in board
    assert "kDeviceStateSpeaking" in board
    assert "virtual bool IsMicrophoneMuted()" in board_header
    assert "virtual bool IsMicrophoneMuted() override" in board
    assert "return muted_.load()" in board
    assert "audio_service.EnableWakeWordDetection(false)" in board
    assert "audio_service.EnableWakeWordDetection(true)" in board
    assert "Board::GetInstance().IsMicrophoneMuted()" in application
    wake_handler = application[application.index("void Application::HandleWakeWordDetectedEvent()"):]
    assert "Ignoring wake word while microphone mute is active" in wake_handler
    assert "audio_service_.EnableWakeWordDetection(false)" in wake_handler
    state_handler = application[application.index("void Application::HandleStateChangedEvent()"):]
    assert "audio_service_.EnableWakeWordDetection(!board.IsMicrophoneMuted())" in state_handler
    assert "audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord() && !board.IsMicrophoneMuted())" in state_handler
    assert "AbortSpeaking" not in board
    assert "SetOutputVolume(0)" not in board


def test_voice_pe_center_button_long_press_enters_wifi_config_mode():
    board = read("main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc")

    assert "center_button_.OnLongPress([this]()" in board
    long_press_handler = board[
        board.index("center_button_.OnLongPress([this]()") :
        board.index("center_button_.OnDoubleClick([this]()")
    ]
    assert "Voice PE center button long pressed" in long_press_handler
    assert "EnterWifiConfigMode()" in long_press_handler
    assert "app.ToggleChatState()" not in long_press_handler
    assert "PlayTestTone" not in long_press_handler


def test_voice_pe_rotary_encoder_schedules_volume_changes_on_main_task():
    board = read("main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc")

    assert '#include "knob.h"' in board
    assert "std::unique_ptr<Knob> knob_" in board
    assert "InitializeKnob()" in board
    assert "VOICE_PE_ENCODER_A_GPIO" in board
    assert "VOICE_PE_ENCODER_B_GPIO" in board
    assert "knob_->OnRotate([this](bool clockwise)" in board
    assert "Application::GetInstance().Schedule([this, clockwise]()" in board
    assert "VOICE_PE_VOLUME_STEP" in board
    assert "std::clamp" in board
    assert "if (volume == current_volume)" in board
    assert "codec->SetOutputVolume(volume)" in board
    assert "SetInputGain" not in board


def test_voice_pe_jack_detect_logs_state_without_changing_audio_route():
    board = read("main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc")

    assert "VOICE_PE_JACK_DETECT_GPIO" in board
    assert "InitializeJackDetect()" in board
    assert "ReadJackDetect()" in board
    assert "jack_inserted_" in board
    assert "Jack detect: %s raw=%d" in board
    assert "inserted" in board
    assert "removed" in board
    assert "AIC3204_DAC" not in board
    assert "SetMuted" not in board


def test_voice_pe_local_wake_uses_preset_nihao_xiaozhi_without_custom_word():
    config_json = read("main/boards/home-assistant-voice-pe/config.json")
    afe_wake = read("main/audio/wake_words/afe_wake_word.cc")

    assert "CONFIG_USE_AFE_WAKE_WORD=y" in config_json
    assert "CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y" in config_json
    assert "CONFIG_USE_CUSTOM_WAKE_WORD" not in config_json
    assert "CONFIG_CUSTOM_WAKE_WORD" not in config_json
    assert "ESP_LOGI(TAG, \"AFE wake word input format: %s\", input_format.c_str())" in afe_wake


def test_voice_pe_reference_channel_is_real_resampled_playback_pcm():
    header = read("main/boards/home-assistant-voice-pe/voice_pe_audio_codec.h")
    source = read("main/boards/home-assistant-voice-pe/voice_pe_audio_codec.cc")
    processor = read("main/audio/processors/afe_audio_processor.cc")

    assert '#include "esp_ae_rate_cvt.h"' in header
    assert "kReferenceRingCapacitySamples = 3200" in source
    assert "kReferencePlaybackDelaySamples = 0" in source
    assert "esp_ae_rate_cvt_open" in source
    assert "esp_ae_rate_cvt_process" in source
    assert "reference_ring_buffer_" in header
    assert "AppendReferenceSamples" in header
    assert "PopReferenceSamples" in header
    assert "LogReferenceProbe" in header
    assert "input_reference_ = AUDIO_INPUT_REFERENCE" in source
    assert "input_channels_ = AUDIO_INPUT_REFERENCE ? 2 : 1" in source
    assert "frames_requested = input_reference_ ? samples / input_channels_ : samples" in source
    assert "dest[i * input_channels_]" in source
    assert "dest[i * input_channels_ + 1]" in source
    assert "AppendReferenceSamples(data, samples_written)" in source
    assert source.index("i2s_channel_write") < source.index("AppendReferenceSamples(data, samples_written)")
    assert "reference probe:" in source
    assert "reference underrun" in source
    assert "reference overflow" in source
    assert "last_reference_append_us_" in header
    assert "kReferenceUnderrunLogWindowUs" in source
    assert "kReferenceStaleAfterUs" in source
    assert "last_reference_append_us_ = esp_timer_get_time()" in source
    assert "now_us - last_reference_append_us_ > kReferenceUnderrunLogWindowUs" in source
    assert "reference_ring_buffer_.clear()" in source
    assert "available = reference_ring_buffer_.size() - kReferencePlaybackDelaySamples" in source
    assert "ESP_LOGI(TAG, \"Audio processor input format: %s\", input_format.c_str())" in processor


def test_voice_pe_uses_hardware_verified_ns_mic_channel_for_wake_and_voice():
    audio_codec_header = read("main/audio/audio_codec.h")
    service_source = read("main/audio/audio_service.cc")
    voice_pe_header = read("main/boards/home-assistant-voice-pe/voice_pe_audio_codec.h")
    voice_pe_source = read("main/boards/home-assistant-voice-pe/voice_pe_audio_codec.cc")

    assert "enum class AudioInputPurpose" in audio_codec_header
    assert "kWakeWord" in audio_codec_header
    assert "kVoiceProcessing" in audio_codec_header
    assert "virtual void SetInputPurpose(AudioInputPurpose purpose)" in audio_codec_header
    assert "SetInputPurpose(AudioInputPurpose purpose) override" in voice_pe_header
    assert "kWakeWordMicSlot = 1" in voice_pe_source
    assert "kVoiceMicSlot = 1" in voice_pe_source
    assert "selected_mic_slot_" in voice_pe_source
    assert "selected_mic_slot_ = kWakeWordMicSlot" in voice_pe_source
    assert "selected_mic_slot_ = kVoiceMicSlot" in voice_pe_source
    assert "bit32_buffer[i * kMicSlotCount + selected_mic_slot_]" in voice_pe_source
    assert "codec_->SetInputPurpose(AudioInputPurpose::kWakeWord)" in service_source
    assert "codec_->SetInputPurpose(AudioInputPurpose::kVoiceProcessing)" in service_source


def test_voice_pe_waits_for_active_playback_before_listening_again():
    service_header = read("main/audio/audio_service.h")
    service_source = read("main/audio/audio_service.cc")
    application = read("main/application.cc")
    tts_stop_handler = application[
        application.index('strcmp(state->valuestring, "stop") == 0') :
        application.index('strcmp(state->valuestring, "sentence_start") == 0')
    ]
    incoming_audio_handler = application[
        application.index("protocol_->OnIncomingAudio") :
        application.index("protocol_->OnAudioChannelOpened")
    ]

    assert "playback_active_" in service_header
    assert "bool HasPlaybackWork()" in service_header
    assert "void ResetVoiceProcessor()" in service_header
    assert "bool AudioService::HasPlaybackWork()" in service_source
    assert "void AudioService::ResetVoiceProcessor()" in service_source
    assert "kPlaybackTailGuardMs = 200" in service_source
    assert "kDecodePacketIdleGuardMs = OPUS_FRAME_DURATION_MS * 2" in service_source
    assert "IsPlaybackTailGuardActiveLocked()" in service_header
    assert "bool AudioService::IsPlaybackTailGuardActiveLocked() const" in service_source
    assert "IsDecodePacketIdleLocked()" in service_header
    assert "bool AudioService::IsDecodePacketIdleLocked() const" in service_source
    assert "decode_packet_seen_" in service_header
    assert "decode_packet_count_" in service_header
    assert "uint32_t GetDecodePacketCount()" in service_header
    assert "uint32_t AudioService::GetDecodePacketCount()" in service_source
    assert "last_decode_packet_time_" in service_header
    assert "playback_active_ = true" in service_source
    assert "playback_active_ = false" in service_source
    assert "decode_active_ = true" in service_source
    assert "decode_active_ = false" in service_source
    assert "#define MAX_PLAYBACK_TASKS_IN_QUEUE 6" in service_header
    assert "BeginPlaybackBuffering(size_t min_frames, int timeout_ms)" in service_source
    assert "Playback buffer release" in service_source
    assert "audio_service_.BeginPlaybackBuffering(kDefaultPlaybackBufferFrames" in application
    assert "playback_duration_ms" in service_source
    assert "task->pcm.size() * 1000" in service_source
    assert "output_rate * output_channels" in service_source
    assert "auto drain_base = last_output_time_ > output_finished_at ? last_output_time_ : output_finished_at" in service_source
    assert "last_output_time_ = drain_base + std::chrono::milliseconds(playback_duration_ms)" in service_source
    assert "decode_packet_seen_ = true" in service_source
    assert "decode_packet_count_++" in service_source
    assert "last_decode_packet_time_ = std::chrono::steady_clock::now()" in service_source
    assert "!IsDecodePacketIdleLocked() || IsPlaybackTailGuardActiveLocked()" in service_source
    assert "min_decode_packet_count" in service_source
    assert "first_packet_timeout_ms" in service_source
    assert "audio_queue_cv_.wait_until(lock, deadline)" in service_source
    wait_method_name = "void AudioService::WaitForPlaybackQueueEmpty("
    assert "while (true)" in service_source[service_source.index(wait_method_name):]
    assert "audio_queue_cv_.notify_all()" in service_source[
        service_source.index("playback_active_ = false") :
    ]
    assert "audio_decode_queue_.empty() && !decode_active_ && audio_playback_queue_.empty() && !playback_active_" in service_source
    assert "codec_->input_reference()" in service_source
    assert "IsDecodePacketIdleLocked() && !IsPlaybackTailGuardActiveLocked()" in service_source
    assert "kDecodePacketIdleGuardMs - static_cast<int>(decode_elapsed_ms)" in service_source
    assert "kPlaybackTailGuardMs - static_cast<int>(output_elapsed_ms)" in service_source
    assert "return elapsed_ms < kPlaybackTailGuardMs" in service_source
    assert "return elapsed_ms >= kDecodePacketIdleGuardMs" in service_source
    assert "vTaskDelay(pdMS_TO_TICKS(wait_ms))" in service_source
    assert "audio_service_.WaitForPlaybackQueueEmpty(" in application
    assert "tts_start_decode_packet_count_ = audio_service_.GetDecodePacketCount()" in application
    assert "audio_service_.ResetDecoderState()" in application
    assert application.index("audio_service_.ResetDecoderState()") < application.index("SetDeviceState(kDeviceStateSpeaking)")
    assert "wait_for_first_tts_audio ? tts_start_decode_packet_count_ : 0" in application
    assert "wait_for_first_tts_audio ? 5000 : 0" in application
    assert "TTS audio before stop: text=\\\"%s\\\" packets=%lu audio_ms=%lu seq=%lu..%lu gaps=%lu plc=%lu ts=%lu..%lu" in application
    assert "TTS drain complete: text=\\\"%s\\\" packets=%lu audio_ms=%lu seq=%lu..%lu gaps=%lu plc=%lu ts=%lu..%lu" in application
    assert "audio_service_.GetDecodePacketSummarySince(tts_start_decode_packet_count_)" in application
    assert tts_stop_handler.index("audio_service_.WaitForPlaybackQueueEmpty(") < tts_stop_handler.index("SetDeviceState(kDeviceStateListening)")
    assert "audio_service_.ResetVoiceProcessor()" in tts_stop_handler
    assert tts_stop_handler.index("audio_service_.ResetVoiceProcessor()") < tts_stop_handler.index("audio_service_.ClearUploadQueues()")
    assert "state == kDeviceStateSpeaking || state == kDeviceStateListening" in incoming_audio_handler


def test_voice_pe_prefers_websocket_when_available():
    application = read("main/application.cc")
    protocol_selection = application[
        application.index("#if CONFIG_BOARD_TYPE_HOME_ASSISTANT_VOICE_PE") :
        application.index("protocol_->OnConnected")
    ]

    assert "Voice PE: using WebSocket protocol" in protocol_selection
    assert "Voice PE: WebSocket config unavailable, using MQTT protocol" in protocol_selection
    assert "diagnostic" not in protocol_selection
    assert "ota_->HasWebsocketConfig()" in protocol_selection
    assert "ota_->HasMqttConfig()" in protocol_selection
    assert "std::make_unique<WebsocketProtocol>()" in protocol_selection
    assert "std::make_unique<MqttProtocol>()" in protocol_selection
    assert protocol_selection.index("ota_->HasWebsocketConfig()") < protocol_selection.index("ota_->HasMqttConfig()")


def test_tts_start_resets_decoder_state_without_clearing_queued_audio():
    service_header = read("main/audio/audio_service.h")
    service_source = read("main/audio/audio_service.cc")
    application = read("main/application.cc")
    tts_start_handler = application[
        application.index('strcmp(state->valuestring, "start") == 0') :
        application.index('strcmp(state->valuestring, "stop") == 0')
    ]
    reset_state_method = service_source[
        service_source.index("void AudioService::ResetDecoderState()") :
        service_source.index("void AudioService::CheckAndUpdateAudioPowerState()")
    ]

    assert "void ResetDecoderState();" in service_header
    assert "audio_service_.ResetDecoderState()" in tts_start_handler
    assert "audio_decode_queue_.clear()" not in reset_state_method
    assert "audio_playback_queue_.clear()" not in reset_state_method
    assert "esp_opus_dec_reset(opus_decoder_)" in reset_state_method
    assert "esp_ae_rate_cvt_reset(output_resampler_)" in reset_state_method
    assert "!audio_decode_queue_.empty() || decode_active_ || !audio_playback_queue_.empty() || playback_active_" in reset_state_method
    assert 'ESP_LOGD(TAG, "Skip decoder state reset' in reset_state_method
    assert 'ESP_LOGW(TAG, "Skip decoder state reset' not in reset_state_method


def test_playback_buffering_is_cleared_on_drain_and_reset_paths():
    service_source = read("main/audio/audio_service.cc")
    wait_method = service_source[
        service_source.index("void AudioService::WaitForPlaybackQueueEmpty(") :
        service_source.index("bool AudioService::IsPlaybackTailGuardActiveLocked()")
    ]
    reset_decoder_method = service_source[
        service_source.index("void AudioService::ResetDecoder()") :
        service_source.index("void AudioService::ResetDecoderState()")
    ]

    assert wait_method.count("playback_buffering_ = false") >= 2
    assert "playback_buffer_min_frames_ = 0" in wait_method
    assert "playback_buffer_timeout_ms_ = 0" in wait_method
    assert "playback_buffer_started_at_ = {}" in wait_method
    assert "playback_buffering_ = false" in reset_decoder_method


def test_udp_packet_loss_uses_opus_plc_and_playback_buffering():
    protocol_header = read("main/protocols/protocol.h")
    mqtt_protocol = read("main/protocols/mqtt_protocol.cc")
    service_source = read("main/audio/audio_service.cc")

    assert "bool loss_concealment = false" in protocol_header
    assert "uint32_t sequence = 0" in protocol_header
    assert "packet->sequence = sequence" in mqtt_protocol
    assert "missing->sequence = missing_sequence" in mqtt_protocol
    assert "sequence <= remote_sequence_" in mqtt_protocol
    assert "sequence > remote_sequence_ + 1" in mqtt_protocol
    assert "Insert PLC audio packet" in mqtt_protocol
    assert "missing->loss_concealment = true" in mqtt_protocol
    assert "ESP_AUDIO_DEC_RECOVERY_PLC" in service_source
    assert "packet_loss_concealment ? nullptr" in service_source
    assert "Playback buffer begin" in service_source
    assert "Playback buffer release" in service_source


def test_tts_summary_reports_text_audio_ms_sequence_gaps_and_plc():
    service_header = read("main/audio/audio_service.h")
    service_source = read("main/audio/audio_service.cc")
    application_header = read("main/application.h")
    application = read("main/application.cc")

    assert "struct DecodePacketInfo" in service_header
    assert "struct DecodePacketSummary" in service_header
    assert "std::deque<DecodePacketInfo> decode_packet_history_" in service_header
    assert "DecodePacketSummary GetDecodePacketSummarySince(uint32_t start_count)" in service_header
    assert "decode_packet_history_.push_back" in service_source
    assert "summary.audio_ms += packet.frame_duration" in service_source
    assert "summary.plc_packets++" in service_source
    assert "summary.sequence_gaps +=" in service_source
    assert "std::string current_tts_text_" in application_header
    assert "current_tts_text_.clear()" in application
    assert "current_tts_text_ += message" in application
    assert "TTS drain complete: text=\\\"%s\\\"" in application


def test_high_frequency_audio_debug_logs_are_not_info_level():
    application = read("main/application.cc")
    service_source = read("main/audio/audio_service.cc")
    mqtt_protocol = read("main/protocols/mqtt_protocol.cc")
    voice_pe_codec = read("main/boards/home-assistant-voice-pe/voice_pe_audio_codec.cc")

    assert 'ESP_LOGI(TAG, "Incoming audio accepted' not in application
    assert 'ESP_LOGI(TAG, "UDP audio in' not in mqtt_protocol
    assert 'ESP_LOGI(TAG, "Decode queue push' not in service_source
    assert 'ESP_LOGI(TAG, "Decode audio' not in service_source
    assert 'ESP_LOGI(TAG, "Output task start' not in service_source
    assert 'ESP_LOGI(TAG, "Output task wrote' not in service_source
    assert 'ESP_LOGI(TAG, "I2S TX write' not in voice_pe_codec


def test_speaking_state_does_not_clear_queued_tts_audio():
    application = read("main/application.cc")
    speaking_handler = application[
        application.index("case kDeviceStateSpeaking:") :
        application.index("case kDeviceStateWifiConfiguring:")
    ]
    incoming_audio_handler = application[
        application.index("protocol_->OnIncomingAudio") :
        application.index("protocol_->OnAudioChannelOpened")
    ]

    assert "state == kDeviceStateSpeaking || state == kDeviceStateListening" in incoming_audio_handler
    assert "audio_service_.ResetDecoder()" not in speaking_handler


def test_starting_voice_processing_does_not_clear_queued_tts_audio():
    service_source = read("main/audio/audio_service.cc")
    enable_voice_processing = service_source[
        service_source.index("void AudioService::EnableVoiceProcessing(bool enable)") :
        service_source.index("void AudioService::ResetVoiceProcessor()")
    ]

    assert "ResetDecoder()" not in enable_voice_processing
    assert "audio_processor_->Start()" in enable_voice_processing
    assert "esp_ae_rate_cvt_reset(input_resampler_)" in enable_voice_processing


def test_voice_pe_does_not_upload_speaker_echo_during_speaking():
    board_header = read("main/boards/common/board.h")
    board = read("main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc")
    application = read("main/application.cc")
    service_header = read("main/audio/audio_service.h")
    service_source = read("main/audio/audio_service.cc")
    speaking_handler = application[
        application.index("case kDeviceStateSpeaking:") :
        application.index("case kDeviceStateWifiConfiguring:")
    ]
    send_audio_handler = application[
        application.index("if (bits & MAIN_EVENT_SEND_AUDIO)") :
        application.index("if (bits & MAIN_EVENT_WAKE_WORD_DETECTED)")
    ]
    tts_stop_handler = application[
        application.index('strcmp(state->valuestring, "stop") == 0') :
        application.index('strcmp(state->valuestring, "sentence_start") == 0')
    ]

    assert "ShouldPauseVoiceProcessingDuringSpeaking" not in board_header
    assert "ShouldPauseVoiceProcessingDuringSpeaking" not in board
    assert "board.ShouldPauseVoiceProcessingDuringSpeaking()" not in speaking_handler
    assert "virtual bool ShouldUploadAudioDuringSpeaking() { return true; }" in board_header
    assert "virtual bool ShouldUploadAudioDuringSpeaking() override" in board
    assert "return false;" in board[board.index("ShouldUploadAudioDuringSpeaking"):]
    assert "ShouldUploadAudioDuringSpeaking()" in send_audio_handler
    assert "state == kDeviceStateListening" in send_audio_handler
    assert "!audio_service_.HasPlaybackWork()" in send_audio_handler
    assert "state == kDeviceStateSpeaking && board.ShouldUploadAudioDuringSpeaking()" in send_audio_handler
    assert "continue;" in send_audio_handler
    assert "if (!Board::GetInstance().ShouldUploadAudioDuringSpeaking())" in tts_stop_handler
    assert "void ClearUploadQueues()" in service_header
    assert "void AudioService::ClearUploadQueues()" in service_source
    clear_upload = service_source[
        service_source.index("void AudioService::ClearUploadQueues()") :
        service_source.index("std::unique_ptr<AudioStreamPacket> AudioService::PopPacketFromSendQueue")
    ]
    assert "audio_send_queue_.clear()" in clear_upload
    assert "kAudioTaskTypeEncodeToSendQueue" in clear_upload
    assert "audio_encode_queue_.erase" in clear_upload
    assert "audio_service_.ClearUploadQueues()" in tts_stop_handler
    assert "while (audio_service_.PopPacketFromSendQueue())" not in tts_stop_handler
    assert "if (listening_mode_ != kListeningModeRealtime)" in speaking_handler
    assert "audio_service_.EnableVoiceProcessing(false)" in speaking_handler
    assert "audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord() && !board.IsMicrophoneMuted())" in speaking_handler


def test_voice_pe_uses_auto_listening_mode_until_realtime_aec_is_verified():
    application = read("main/application.cc")
    default_mode = application[
        application.index("ListeningMode Application::GetDefaultListeningMode() const") :
        application.index("void Application::Reboot()")
    ]

    assert "Board::GetInstance().ShouldUploadAudioDuringSpeaking()" in default_mode
    assert "return kListeningModeAutoStop" in default_mode
    assert default_mode.index("Board::GetInstance().ShouldUploadAudioDuringSpeaking()") < default_mode.index("kListeningModeRealtime")


def test_voice_pe_realtime_voice_processor_does_not_block_afe_fetch_on_encode_backpressure():
    service_header = read("main/audio/audio_service.h")
    service_source = read("main/audio/audio_service.cc")
    processor_callback = service_source[
        service_source.index("audio_processor_->OnOutput") :
        service_source.index("audio_processor_->OnVadStateChange")
    ]
    push_method = service_source[
        service_source.index("void AudioService::PushTaskToEncodeQueue") :
        service_source.index("bool AudioService::PushPacketToDecodeQueue")
    ]

    assert "void PushTaskToEncodeQueue(AudioTaskType type, std::vector<int16_t>&& pcm, bool wait = true)" in service_header
    assert "PushTaskToEncodeQueue(kAudioTaskTypeEncodeToSendQueue, std::move(data), !codec_->input_reference())" in processor_callback
    assert "if (!wait && audio_encode_queue_.size() >= MAX_ENCODE_TASKS_IN_QUEUE)" in push_method
    assert "audio_encode_queue_.pop_front()" in push_method
    assert "if (wait) {" in push_method


def test_voice_pe_resets_afe_processor_buffers_before_uploading_after_tts():
    service_header = read("main/audio/audio_service.h")
    service_source = read("main/audio/audio_service.cc")
    processor_header = read("main/audio/audio_processor.h")
    afe_processor = read("main/audio/processors/afe_audio_processor.cc")

    reset_method = service_source[
        service_source.index("void AudioService::ResetVoiceProcessor()") :
        service_source.index("bool AudioService::HasPlaybackWork()")
    ]

    assert "virtual void Reset() = 0" in processor_header
    assert "void AudioService::ResetVoiceProcessor()" in service_source
    assert "audio_processor_initialized_" in reset_method
    assert "AS_EVENT_AUDIO_PROCESSOR_RUNNING" in reset_method
    assert "audio_processor_->Reset()" in reset_method
    assert "audio_input_need_warmup_ = true" in reset_method
    assert "void AfeAudioProcessor::Reset()" in afe_processor
    afe_reset_method = afe_processor[
        afe_processor.index("void AfeAudioProcessor::Reset()") :
        afe_processor.index("bool AfeAudioProcessor::IsRunning()")
    ]
    assert "afe_iface_->reset_buffer(afe_data_)" in afe_reset_method
    assert "input_buffer_.clear()" in afe_reset_method
    assert "output_buffer_.clear()" in afe_reset_method
    assert "is_speaking_ = false" in afe_reset_method


def test_listening_state_does_not_restart_realtime_stream_after_each_tts_stop():
    application = read("main/application.cc")
    listening_handler = application[
        application.index("case kDeviceStateListening:") :
        application.index("#ifdef CONFIG_WAKE_WORD_DETECTION_IN_LISTENING")
    ]

    assert listening_handler.count("protocol_->SendStartListening(listening_mode_)") == 1
    assert "if (play_popup_on_listening_ || !audio_service_.IsAudioProcessorRunning())" in listening_handler
    assert "audio_service_.EnableVoiceProcessing(false)" not in listening_handler
    assert "while (audio_service_.PopPacketFromSendQueue())" not in listening_handler
    assert listening_handler.index("protocol_->SendStartListening(listening_mode_)") < listening_handler.index("audio_service_.EnableVoiceProcessing(true)")
    assert "audio_service_.EnableVoiceProcessing(true)" in listening_handler


def test_voice_pe_has_low_rate_pipeline_diagnostics_for_listening_no_response():
    service_header = read("main/audio/audio_service.h")
    service_source = read("main/audio/audio_service.cc")
    application = read("main/application.cc")

    for field in [
        "raw_input_rms",
        "processor_feed_count",
        "processor_output_count",
        "processor_output_rms",
        "encode_drop_count",
        "send_count",
        "send_fail_count",
    ]:
        assert field in service_header

    assert "void LogVoicePipelineProbe()" in service_header
    assert "float CalculateRms(const std::vector<int16_t>& data)" in service_source
    assert "float CalculateMicRms(const std::vector<int16_t>& data, int channels)" in service_source
    assert "debug_statistics_.raw_input_rms = CalculateMicRms(data, codec_->input_channels())" in service_source
    assert "debug_statistics_.processor_output_rms = CalculateRms(data)" in service_source
    assert "void NotifyPacketSent(bool success)" in service_header
    assert "bool sent = protocol_ && protocol_->SendAudio(std::move(packet))" in application
    assert "NotifyPacketSent(sent)" in application
    assert "Failed to send audio packet" in application
    assert "voice pipeline:" in service_source
    assert "raw_rms=%.1f" in service_source
    assert "out_rms=%.1f" in service_source
    assert "sent=%u send_fail=%u sendq=%u encodeq=%u" in service_source
    assert "LogVoicePipelineProbe()" in service_source
    assert "Listening started: mode=%d processor_running=%d" in application
