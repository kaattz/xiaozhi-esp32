#include "application.h"
#include "button.h"
#include "config.h"
#include "voice_pe_audio_codec.h"
#include "voice_pe_xmos.h"
#include "wifi_board.h"

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <memory>

#define TAG "HomeAssistantVoicePe"

class HomeAssistantVoicePeBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t internal_i2c_bus_ = nullptr;
    std::unique_ptr<VoicePeXmos> xmos_;
    Button center_button_;

    void InitializeInternalI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)VOICE_PE_INTERNAL_I2C_PORT,
            .sda_io_num = VOICE_PE_INTERNAL_I2C_SDA_GPIO,
            .scl_io_num = VOICE_PE_INTERNAL_I2C_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &internal_i2c_bus_));
        ESP_LOGI(TAG, "Internal I2C initialized on SDA=%d SCL=%d",
            VOICE_PE_INTERNAL_I2C_SDA_GPIO, VOICE_PE_INTERNAL_I2C_SCL_GPIO);
    }

    void InitializeXmos() {
        xmos_ = std::make_unique<VoicePeXmos>(internal_i2c_bus_, VOICE_PE_XMOS_RESET_GPIO);
        ESP_ERROR_CHECK(xmos_->Initialize());
    }

    void InitializeButtons() {
        center_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        center_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            auto state = app.GetDeviceState();
            if (state != kDeviceStateIdle && state != kDeviceStateWifiConfiguring) {
                ESP_LOGW(TAG, "Ignoring Voice PE speaker test outside idle/config state");
                return;
            }
            auto* codec = static_cast<VoicePeAudioCodec*>(GetAudioCodec());
            codec->PlayTestTone(1000);
        });
    }

public:
    HomeAssistantVoicePeBoard() : center_button_(BOOT_BUTTON_GPIO) {
        InitializeInternalI2c();
        InitializeXmos();
        InitializeButtons();
        ESP_LOGI(TAG, "Home Assistant Voice PE board initialized");
    }

    virtual AudioCodec* GetAudioCodec() override {
        static VoicePeAudioCodec audio_codec(internal_i2c_bus_);
        return &audio_codec;
    }
};

DECLARE_BOARD(HomeAssistantVoicePeBoard);
