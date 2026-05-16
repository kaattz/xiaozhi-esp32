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
    assert "kSelectedMicSlot = 1" in source
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


def test_voice_pe_interaction_gpio_constants_are_declared():
    config = read("main/boards/home-assistant-voice-pe/config.h")
    config_json = read("main/boards/home-assistant-voice-pe/config.json")

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
        ("VOICE_PE_VOLUME_STEP", "10"),
    ]:
        assert f"#define {name}" in config
        assert value in config

    assert "CONFIG_WAKE_WORD_DISABLED=y" in config_json
    assert "CONFIG_USE_DEVICE_AEC=y" not in config_json


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


def test_voice_pe_mute_switch_blocks_microphone_entry_without_muting_speaker():
    board = read("main/boards/home-assistant-voice-pe/home_assistant_voice_pe_board.cc")

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
    assert "AbortSpeaking" not in board
    assert "SetOutputVolume(0)" not in board


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
