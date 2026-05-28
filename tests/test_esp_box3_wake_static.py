from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_esp_box3_wake_input_probe_is_scoped_and_low_frequency():
    header = read("main/audio/audio_service.h")
    source = read("main/audio/audio_service.cc")

    assert "kWakeWordInputProbeIntervalUs" in source
    assert "void AudioService::LogWakeWordInputProbe()" in source
    assert "last_wake_word_input_probe_us_" in header
    assert "LogWakeWordInputProbe();" in source
    assert "#if CONFIG_BOARD_TYPE_ESP_BOX_3" in source
    assert 'ESP_LOGD(TAG, "wake input probe: input=%u raw_rms=%.1f channels=%d"' in source
    assert 'ESP_LOGI(TAG, "wake input probe: input=%u raw_rms=%.1f channels=%d"' not in source


def test_esp_box3_keeps_original_tdm_reference_capture():
    board = read("main/boards/esp-box-3/esp_box3_board.cc")
    config = read("main/boards/esp-box-3/config.h")
    codec_header = read("main/audio/codecs/box_audio_codec.h")
    codec_source = read("main/audio/codecs/box_audio_codec.cc")

    assert "#define AUDIO_INPUT_REFERENCE    true" in config
    assert "AUDIO_INPUT_TDM" not in config
    assert "AUDIO_CODEC_ES7210_MIC_SELECT" not in config
    assert "AUDIO_INPUT_TDM" not in board
    assert "AUDIO_CODEC_ES7210_MIC_SELECT" not in board

    assert "bool input_tdm" not in codec_header
    assert "uint8_t es7210_mic_selected" not in codec_header
    assert "input_tdm_" not in codec_source
    assert "ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4" in codec_source
    assert "i2s_channel_init_tdm_mode(rx_handle_, &tdm_cfg)" in codec_source
    assert "i2s_channel_init_std_mode(rx_handle_, &rx_std_cfg)" not in codec_source
    assert ".channel = 4" in codec_source
    assert ".channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0)" in codec_source
