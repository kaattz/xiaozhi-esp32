#include "aic3204_audio_dac.h"
#include "config.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>

#define TAG "Aic3204AudioDac"

namespace {
// Source: esphome/esphome@35631be260c0fd6fae1e4c945f16790979ba777c
// esphome/components/aic3204/aic3204.h and aic3204.cpp.

static_assert(VOICE_PE_AIC3204_I2C_ADDR == 0x18, "Voice PE AIC3204 I2C address drifted");

constexpr uint8_t kAic3204I2cAddress = 0x18;

// Page 0
constexpr uint8_t AIC3204_PAGE_CTRL = 0x00;
constexpr uint8_t AIC3204_SW_RST = 0x01;
constexpr uint8_t AIC3204_NDAC = 0x0B;
constexpr uint8_t AIC3204_MDAC = 0x0C;
constexpr uint8_t AIC3204_DOSR = 0x0E;
constexpr uint8_t AIC3204_CODEC_IF = 0x1B;
constexpr uint8_t AIC3204_AUDIO_IF_4 = 0x1F;
constexpr uint8_t AIC3204_AUDIO_IF_5 = 0x20;
constexpr uint8_t AIC3204_SCLK_MFP3 = 0x38;
constexpr uint8_t AIC3204_DAC_SIG_PROC = 0x3C;
constexpr uint8_t AIC3204_DAC_CH_SET1 = 0x3F;
constexpr uint8_t AIC3204_DAC_CH_SET2 = 0x40;
constexpr uint8_t AIC3204_DACL_VOL_D = 0x41;
constexpr uint8_t AIC3204_DACR_VOL_D = 0x42;

// Page 1
constexpr uint8_t AIC3204_PWR_CFG = 0x01;
constexpr uint8_t AIC3204_LDO_CTRL = 0x02;
constexpr uint8_t AIC3204_PLAY_CFG1 = 0x03;
constexpr uint8_t AIC3204_PLAY_CFG2 = 0x04;
constexpr uint8_t AIC3204_OP_PWR_CTRL = 0x09;
constexpr uint8_t AIC3204_CM_CTRL = 0x0A;
constexpr uint8_t AIC3204_HPL_ROUTE = 0x0C;
constexpr uint8_t AIC3204_HPR_ROUTE = 0x0D;
constexpr uint8_t AIC3204_LOL_ROUTE = 0x0E;
constexpr uint8_t AIC3204_LOR_ROUTE = 0x0F;
constexpr uint8_t AIC3204_HPL_GAIN = 0x10;
constexpr uint8_t AIC3204_HPR_GAIN = 0x11;
constexpr uint8_t AIC3204_LOL_DRV_GAIN = 0x12;
constexpr uint8_t AIC3204_LOR_DRV_GAIN = 0x13;
constexpr uint8_t AIC3204_HP_START = 0x14;
constexpr uint8_t AIC3204_REF_STARTUP = 0x7B;
} // namespace

Aic3204AudioDac::Aic3204AudioDac(i2c_master_bus_handle_t i2c_bus) {
    i2c_device_config_t i2c_device_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = kAic3204I2cAddress,
        .scl_speed_hz = 400 * 1000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &i2c_device_cfg, &i2c_device_));
}

esp_err_t Aic3204AudioDac::WriteReg(uint8_t reg, uint8_t value) {
    const uint8_t buffer[] = {reg, value};
    esp_err_t err = i2c_master_transmit(i2c_device_, buffer, sizeof(buffer), 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Write reg 0x%02x = 0x%02x failed: %s", reg, value, esp_err_to_name(err));
    }
    return err;
}

esp_err_t Aic3204AudioDac::SelectPage(uint8_t page) {
    return WriteReg(AIC3204_PAGE_CTRL, page);
}

uint8_t Aic3204AudioDac::VolumeToRegisterValue(int volume) const {
    constexpr int dvc_min = -127;
    constexpr int dvc_max = 48;
    const int safe_volume = std::clamp(volume, 0, 100);
    const int value = dvc_min + (safe_volume * (dvc_max - dvc_min) / 100);
    return static_cast<uint8_t>(std::clamp(value, dvc_min, dvc_max));
}

esp_err_t Aic3204AudioDac::Initialize() {
    if (initialized_) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing AIC3204");

    esp_err_t err = ESP_OK;
    auto write = [&](uint8_t reg, uint8_t value) -> bool {
        err = WriteReg(reg, value);
        return err == ESP_OK;
    };

    if (!write(AIC3204_PAGE_CTRL, 0x00) ||
        !write(AIC3204_SW_RST, 0x01) ||
        !write(AIC3204_NDAC, 0x82) ||
        !write(AIC3204_MDAC, 0x82) ||
        !write(AIC3204_DOSR, 0x80) ||
        !write(AIC3204_CODEC_IF, 0x30) ||
        !write(AIC3204_SCLK_MFP3, 0x02) ||
        !write(AIC3204_AUDIO_IF_4, 0x01) ||
        !write(AIC3204_AUDIO_IF_5, 0x01) ||
        !write(AIC3204_DAC_SIG_PROC, 0x01) ||
        !write(AIC3204_PAGE_CTRL, 0x01) ||
        !write(AIC3204_LDO_CTRL, 0x09) ||
        !write(AIC3204_PWR_CFG, 0x08) ||
        !write(AIC3204_LDO_CTRL, 0x01) ||
        !write(AIC3204_CM_CTRL, 0x40) ||
        !write(AIC3204_PLAY_CFG1, 0x00) ||
        !write(AIC3204_PLAY_CFG2, 0x00) ||
        !write(AIC3204_REF_STARTUP, 0x01) ||
        !write(AIC3204_HP_START, 0x25) ||
        !write(AIC3204_HPL_ROUTE, 0x08) ||
        !write(AIC3204_HPR_ROUTE, 0x08) ||
        !write(AIC3204_LOL_ROUTE, 0x08) ||
        !write(AIC3204_LOR_ROUTE, 0x08) ||
        !write(AIC3204_HPL_GAIN, 0x3e) ||
        !write(AIC3204_HPR_GAIN, 0x3e) ||
        !write(AIC3204_LOL_DRV_GAIN, 0x00) ||
        !write(AIC3204_LOR_DRV_GAIN, 0x00) ||
        !write(AIC3204_OP_PWR_CTRL, 0x3c)) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(2500));

    if (!write(AIC3204_PAGE_CTRL, 0x00) ||
        !write(AIC3204_DAC_CH_SET1, 0xd4)) {
        return err;
    }

    err = SetVolume(volume_);
    if (err != ESP_OK) {
        return err;
    }

    err = SetMuted(muted_);
    if (err != ESP_OK) {
        return err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Aic3204AudioDac::SetMuted(bool muted) {
    muted_ = muted;
    esp_err_t err = SelectPage(0x00);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t mute_mode = muted_ ? 0x0c : 0x00;
    return WriteReg(AIC3204_DAC_CH_SET2, mute_mode);
}

esp_err_t Aic3204AudioDac::SetVolume(int volume) {
    volume_ = std::clamp(volume, 0, 100);
    esp_err_t err = SelectPage(0x00);
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t volume_reg = VolumeToRegisterValue(volume_);
    err = WriteReg(AIC3204_DACL_VOL_D, volume_reg);
    if (err != ESP_OK) {
        return err;
    }
    return WriteReg(AIC3204_DACR_VOL_D, volume_reg);
}
