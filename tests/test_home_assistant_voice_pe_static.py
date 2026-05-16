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
