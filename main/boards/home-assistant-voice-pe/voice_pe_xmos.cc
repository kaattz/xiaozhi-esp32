#include "voice_pe_xmos.h"
#include "config.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "VoicePeXmos"

namespace {
static_assert(VOICE_PE_XMOS_I2C_ADDR == 0x42, "Voice PE XMOS I2C address drifted");

constexpr uint8_t kXmosI2cAddress = 0x42;
constexpr uint8_t kDfuControllerServicerResid = 240;
constexpr uint8_t kConfigurationServicerResid = 241;
constexpr uint8_t kReadBit = 0x80;
constexpr uint8_t kDfuGetVersion = 88;
constexpr uint8_t kCtrlDone = 0;
constexpr uint8_t kChannel0PipelineStage = 0x30;
constexpr uint8_t kChannel1PipelineStage = 0x40;
constexpr uint8_t kPipelineStageAgc = 4;
constexpr uint8_t kPipelineStageNs = 3;
} // namespace

VoicePeXmos::VoicePeXmos(i2c_master_bus_handle_t i2c_bus, gpio_num_t reset_gpio)
    : reset_gpio_(reset_gpio) {
    i2c_device_config_t i2c_device_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = kXmosI2cAddress,
        .scl_speed_hz = 400 * 1000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &i2c_device_cfg, &i2c_device_));
}

esp_err_t VoicePeXmos::Reset() {
    gpio_config_t reset_cfg = {
        .pin_bit_mask = 1ULL << reset_gpio_,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&reset_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure XMOS reset GPIO%d: %s", reset_gpio_, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Resetting XMOS on GPIO%d", reset_gpio_);
    gpio_set_level(reset_gpio_, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(reset_gpio_, 0);
    return ESP_OK;
}

esp_err_t VoicePeXmos::Initialize() {
    esp_err_t err = Reset();
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    const TickType_t retry_delay = pdMS_TO_TICKS(250);
    const TickType_t timeout = pdMS_TO_TICKS(4000);
    const TickType_t start = xTaskGetTickCount();
    Version version;

    while ((xTaskGetTickCount() - start) <= timeout) {
        err = ReadVersion(version);
        if (err == ESP_OK) {
            version_ = version;
            ESP_LOGI(TAG, "XMOS firmware version %u.%u.%u", version_.major, version_.minor, version_.patch);
            return WritePipelineStages();
        }
        ESP_LOGW(TAG, "XMOS version read failed: %s; retrying", esp_err_to_name(err));
        vTaskDelay(retry_delay);
    }

    ESP_LOGE(TAG, "XMOS version read timed out after 4000ms");
    return ESP_ERR_TIMEOUT;
}

esp_err_t VoicePeXmos::ReadVersion(Version& version) {
    const uint8_t version_req[] = {
        kDfuControllerServicerResid,
        static_cast<uint8_t>(kDfuGetVersion | kReadBit),
        4,
    };
    uint8_t version_resp[4] = {};

    esp_err_t err = i2c_master_transmit(i2c_device_, version_req, sizeof(version_req), 100);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_master_receive(i2c_device_, version_resp, sizeof(version_resp), 100);
    if (err != ESP_OK) {
        return err;
    }
    if (version_resp[0] != kCtrlDone) {
        ESP_LOGW(TAG, "XMOS version response status is %u, expected %u", version_resp[0], kCtrlDone);
        return ESP_ERR_INVALID_RESPONSE;
    }

    version.major = version_resp[1];
    version.minor = version_resp[2];
    version.patch = version_resp[3];
    return ESP_OK;
}

esp_err_t VoicePeXmos::WritePipelineStages() {
    uint8_t stage_set[] = {
        kConfigurationServicerResid,
        kChannel0PipelineStage,
        1,
        kPipelineStageAgc,
    };

    esp_err_t err = i2c_master_transmit(i2c_device_, stage_set, sizeof(stage_set), 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write XMOS channel 0 pipeline stage: %s", esp_err_to_name(err));
        return err;
    }

    stage_set[1] = kChannel1PipelineStage;
    stage_set[3] = kPipelineStageNs;
    err = i2c_master_transmit(i2c_device_, stage_set, sizeof(stage_set), 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write XMOS channel 1 pipeline stage: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "XMOS pipeline stages set: channel0=AGC channel1=NS");
    return ESP_OK;
}
