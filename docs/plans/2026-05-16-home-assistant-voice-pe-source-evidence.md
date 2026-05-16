# Home Assistant Voice PE Source Evidence

## Source Pins

| Item | Value | Source |
|---|---|---|
| Voice PE firmware commit | `1efed69c6ad2142023536136f3205dace7de246a` | `https://github.com/esphome/home-assistant-voice-pe/tree/1efed69c6ad2142023536136f3205dace7de246a` |
| ESPHome AIC3204 commit | `35631be260c0fd6fae1e4c945f16790979ba777c` | `https://github.com/esphome/esphome/tree/35631be260c0fd6fae1e4c945f16790979ba777c` |
| Voice PE YAML | `home-assistant-voice.yaml` | `https://raw.githubusercontent.com/esphome/home-assistant-voice-pe/1efed69c6ad2142023536136f3205dace7de246a/home-assistant-voice.yaml` |
| Voice kit component | `esphome/components/voice_kit/*` | `https://github.com/esphome/home-assistant-voice-pe/tree/1efed69c6ad2142023536136f3205dace7de246a/esphome/components/voice_kit` |
| AIC3204 implementation | `esphome/components/aic3204/aic3204.cpp` | `https://github.com/esphome/esphome/blob/35631be260c0fd6fae1e4c945f16790979ba777c/esphome/components/aic3204/aic3204.cpp` |
| AIC3204 registers | `esphome/components/aic3204/aic3204.h` | `https://github.com/esphome/esphome/blob/35631be260c0fd6fae1e4c945f16790979ba777c/esphome/components/aic3204/aic3204.h` |
| AIC3204 schema | `esphome/components/aic3204/audio_dac.py` | `https://github.com/esphome/esphome/blob/35631be260c0fd6fae1e4c945f16790979ba777c/esphome/components/aic3204/audio_dac.py` |

## Hardware Constants

| Project field | Value | Source field |
|---|---|---|
| `VOICE_PE_INTERNAL_I2C_SDA` | GPIO5 | YAML `i2c.internal_i2c.sda` |
| `VOICE_PE_INTERNAL_I2C_SCL` | GPIO6 | YAML `i2c.internal_i2c.scl` |
| `VOICE_PE_XMOS_I2C_ADDR` | `0x42` | `voice_kit/__init__.py` uses `i2c_device_schema(0x42)` |
| `VOICE_PE_XMOS_RESET_GPIO` | GPIO4 | YAML `voice_kit.reset_pin` |
| `VOICE_PE_MIC_BCLK_GPIO` | GPIO13 | YAML `i2s_input.i2s_bclk_pin` |
| `VOICE_PE_MIC_LRCLK_GPIO` | GPIO14 | YAML `i2s_input.i2s_lrclk_pin` |
| `VOICE_PE_MIC_DIN_GPIO` | GPIO15 | YAML `microphone.i2s_din_pin` |
| `VOICE_PE_MIC_SAMPLE_RATE` | 16000 | YAML `microphone.sample_rate` |
| `VOICE_PE_MIC_BITS` | 32bit | YAML `microphone.bits_per_sample` |
| `VOICE_PE_MIC_CHANNEL` | stereo | YAML `microphone.channel` |
| `VOICE_PE_MIC_I2S_MODE` | secondary/slave | YAML `microphone.i2s_mode` |
| `VOICE_PE_SPK_BCLK_GPIO` | GPIO8 | YAML `i2s_output.i2s_bclk_pin` |
| `VOICE_PE_SPK_LRCLK_GPIO` | GPIO7 | YAML `i2s_output.i2s_lrclk_pin` |
| `VOICE_PE_SPK_DOUT_GPIO` | GPIO10 | YAML `speaker.i2s_dout_pin` |
| `VOICE_PE_SPK_SAMPLE_RATE` | 48000 | YAML `speaker.sample_rate` |
| `VOICE_PE_SPK_BITS` | 32bit | YAML `speaker.bits_per_sample` |
| `VOICE_PE_SPK_CHANNEL` | stereo | YAML `speaker.channel` |
| `VOICE_PE_SPK_I2S_MODE` | secondary/slave | YAML `speaker.i2s_mode` |
| `VOICE_PE_AMP_ENABLE_GPIO` | GPIO47 | YAML `switch.internal_speaker_amp.pin` |
| `VOICE_PE_AIC3204_I2C_ADDR` | `0x18` | `aic3204/audio_dac.py` uses `i2c_device_schema(0x18)` |

## XMOS Protocol Constants

| Project field | Value | Source field |
|---|---|---|
| DFU servicer resource | `240` | `voice_kit.h` `DFU_CONTROLLER_SERVICER_RESID` |
| Configuration servicer resource | `241` | `voice_kit.h` `CONFIGURATION_SERVICER_RESID` |
| Read bit | `0x80` | `voice_kit.h` read-bit constants |
| Get version command | `88` | `voice_kit.h` `DFU_GETVERSION` |
| Version request | `{240, 88 | 0x80, 4}` | `voice_kit.cpp` `dfu_get_version_()` |
| Channel 0 stage register | `0x30` | `voice_kit.h` `CHANNEL_0_PIPELINE_STAGE` |
| Channel 1 stage register | `0x40` | `voice_kit.h` `CHANNEL_1_PIPELINE_STAGE` |
| Pipeline stages | `NONE=0`, `AEC=1`, `IC=2`, `NS=3`, `AGC=4` | `voice_kit.h` `PipelineStages` |
| ESPHome default channel 0 stage | `AGC` | `voice_kit/__init__.py` default |
| ESPHome default channel 1 stage | `NS` | `voice_kit/__init__.py` default |

First stage does not perform XMOS DFU. It only resets XMOS and reads version/health state.

## AIC3204 Init Sequence

The register order below comes from fixed commit `35631be260c0fd6fae1e4c945f16790979ba777c`, file `esphome/components/aic3204/aic3204.cpp`. Register names come from `aic3204.h`.

| Step | Page | Register | Value |
|---|---:|---|---:|
| 1 | 0 | `AIC3204_PAGE_CTRL` | `0x00` |
| 2 | 0 | `AIC3204_SW_RST` | `0x01` |
| 3 | 0 | `AIC3204_NDAC` | `0x82` |
| 4 | 0 | `AIC3204_MDAC` | `0x82` |
| 5 | 0 | `AIC3204_DOSR` | `0x80` |
| 6 | 0 | `AIC3204_CODEC_IF` | `0x30` |
| 7 | 0 | `AIC3204_SCLK_MFP3` | `0x02` |
| 8 | 0 | `AIC3204_AUDIO_IF_4` | `0x01` |
| 9 | 0 | `AIC3204_AUDIO_IF_5` | `0x01` |
| 10 | 0 | `AIC3204_DAC_SIG_PROC` | `0x01` |
| 11 | 1 | `AIC3204_PAGE_CTRL` | `0x01` |
| 12 | 1 | `AIC3204_LDO_CTRL` | `0x09` |
| 13 | 1 | `AIC3204_PWR_CFG` | `0x08` |
| 14 | 1 | `AIC3204_LDO_CTRL` | `0x01` |
| 15 | 1 | `AIC3204_CM_CTRL` | `0x40` |
| 16 | 1 | `AIC3204_PLAY_CFG1` | `0x00` |
| 17 | 1 | `AIC3204_PLAY_CFG2` | `0x00` |
| 18 | 1 | `AIC3204_REF_STARTUP` | `0x01` |
| 19 | 1 | `AIC3204_HP_START` | `0x25` |
| 20 | 1 | `AIC3204_HPL_ROUTE` | `0x08` |
| 21 | 1 | `AIC3204_HPR_ROUTE` | `0x08` |
| 22 | 1 | `AIC3204_LOL_ROUTE` | `0x08` |
| 23 | 1 | `AIC3204_LOR_ROUTE` | `0x08` |
| 24 | 1 | `AIC3204_HPL_GAIN` | `0x3e` |
| 25 | 1 | `AIC3204_HPR_GAIN` | `0x3e` |
| 26 | 1 | `AIC3204_LOL_DRV_GAIN` | `0x00` |
| 27 | 1 | `AIC3204_LOR_DRV_GAIN` | `0x00` |
| 28 | 1 | `AIC3204_OP_PWR_CTRL` | `0x3c` |
| 29 | 0 | `AIC3204_PAGE_CTRL` | `0x00` |
| 30 | 0 | `AIC3204_DAC_CH_SET1` | `0xd4` |
| 31 | 0 | `AIC3204_DACL_VOL_D` | volume-derived |
| 32 | 0 | `AIC3204_DACR_VOL_D` | volume-derived |
| 33 | 0 | `AIC3204_DAC_CH_SET2` | mute-derived |

## Non-imported ESPHome Features

| ESPHome feature | First-stage decision |
|---|---|
| `voice_assistant` | Not imported |
| `micro_wake_word` | Not imported |
| LED scripts/effects | Not imported |
| `sendspin` delay/group media pipeline | Not imported |
| XMOS DFU firmware update | Not imported |
