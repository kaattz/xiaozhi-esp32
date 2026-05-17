#include "application.h"
#include "button.h"
#include "config.h"
#include "knob.h"
#include "led/circular_strip.h"
#include "voice_pe_audio_codec.h"
#include "voice_pe_xmos.h"
#include "wifi_board.h"

#include <algorithm>
#include <atomic>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <memory>

#define TAG "HomeAssistantVoicePe"

static constexpr int kMutePollIntervalUs = 50 * 1000;
static constexpr int kDebounceStableSamples = 2;

class HomeAssistantVoicePeBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t internal_i2c_bus_ = nullptr;
    std::unique_ptr<VoicePeXmos> xmos_;
    std::unique_ptr<Knob> knob_;
    std::unique_ptr<CircularStrip> led_strip_;
    Button center_button_;
    esp_timer_handle_t input_poll_timer_ = nullptr;
    std::atomic<bool> muted_{false};
    std::atomic<bool> jack_inserted_{false};
    bool mute_candidate_ = false;
    bool jack_candidate_ = false;
    int mute_candidate_count_ = 0;
    int jack_candidate_count_ = 0;

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

    void InitializeLed() {
        gpio_config_t led_power_cfg = {
            .pin_bit_mask = 1ULL << VOICE_PE_LED_POWER_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&led_power_cfg));
        ESP_ERROR_CHECK(gpio_set_level(VOICE_PE_LED_POWER_GPIO, 1));

        led_strip_ = std::make_unique<CircularStrip>(VOICE_PE_LED_DATA_GPIO, VOICE_PE_LED_COUNT);
        ESP_LOGI(TAG, "LED ring initialized on data GPIO=%d power GPIO=%d count=%d",
            VOICE_PE_LED_DATA_GPIO, VOICE_PE_LED_POWER_GPIO, VOICE_PE_LED_COUNT);
    }

    void ConfigureInputGpio(gpio_num_t gpio) {
        gpio_config_t input_cfg = {
            .pin_bit_mask = 1ULL << gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&input_cfg));
    }

    bool ReadMuteSwitch() const {
        return gpio_get_level(VOICE_PE_MUTE_GPIO) == VOICE_PE_MUTE_ACTIVE_LEVEL;
    }

    bool ReadJackDetect() const {
        return gpio_get_level(VOICE_PE_JACK_DETECT_GPIO) == VOICE_PE_JACK_INSERTED_LEVEL;
    }

    void InitializeMuteSwitch() {
        ConfigureInputGpio(VOICE_PE_MUTE_GPIO);
        auto muted = ReadMuteSwitch();
        muted_.store(muted);
        mute_candidate_ = muted;
        mute_candidate_count_ = kDebounceStableSamples;
        ESP_LOGI(TAG, "Mute switch raw=%d muted=%s",
            gpio_get_level(VOICE_PE_MUTE_GPIO), muted ? "true" : "false");
    }

    void InitializeJackDetect() {
        ConfigureInputGpio(VOICE_PE_JACK_DETECT_GPIO);
        auto inserted = ReadJackDetect();
        jack_inserted_.store(inserted);
        jack_candidate_ = inserted;
        jack_candidate_count_ = kDebounceStableSamples;
        ESP_LOGI(TAG, "Jack detect raw=%d inserted=%s",
            gpio_get_level(VOICE_PE_JACK_DETECT_GPIO), inserted ? "true" : "false");
    }

    void InitializeInputPolling() {
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                auto* board = static_cast<HomeAssistantVoicePeBoard*>(arg);
                board->PollHardwareInputs();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "voice_pe_inputs",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &input_poll_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(input_poll_timer_, kMutePollIntervalUs));
    }

    void PollHardwareInputs() {
        PollMuteSwitch();
        PollJackDetect();
    }

    void PollMuteSwitch() {
        auto muted = ReadMuteSwitch();
        if (muted == mute_candidate_) {
            if (mute_candidate_count_ < kDebounceStableSamples) {
                mute_candidate_count_++;
            }
        } else {
            mute_candidate_ = muted;
            mute_candidate_count_ = 1;
        }

        if (mute_candidate_count_ == kDebounceStableSamples && muted_.load() != mute_candidate_) {
            muted_.store(mute_candidate_);
            OnMuteChanged(mute_candidate_);
        }
    }

    void PollJackDetect() {
        auto inserted = ReadJackDetect();
        if (inserted == jack_candidate_) {
            if (jack_candidate_count_ < kDebounceStableSamples) {
                jack_candidate_count_++;
            }
        } else {
            jack_candidate_ = inserted;
            jack_candidate_count_ = 1;
        }

        if (jack_candidate_count_ == kDebounceStableSamples && jack_inserted_.load() != jack_candidate_) {
            jack_inserted_.store(jack_candidate_);
            ESP_LOGI(TAG, "Jack detect: %s raw=%d",
                jack_candidate_ ? "inserted" : "removed",
                gpio_get_level(VOICE_PE_JACK_DETECT_GPIO));
        }
    }

    void OnMuteChanged(bool muted) {
        ESP_LOGI(TAG, "Mute switch changed: muted=%s raw=%d",
            muted ? "true" : "false", gpio_get_level(VOICE_PE_MUTE_GPIO));

        Application::GetInstance().Schedule([this]() {
            auto& app = Application::GetInstance();
            auto& audio_service = app.GetAudioService();
            auto state = app.GetDeviceState();
            if (muted_.load()) {
                audio_service.EnableWakeWordDetection(false);
                if (state == kDeviceStateListening) {
                    ESP_LOGI(TAG, "Stopping listening because Voice PE mute switch is active");
                    app.StopListening();
                } else if (state == kDeviceStateConnecting) {
                    ESP_LOGI(TAG, "Canceling connecting because Voice PE mute switch is active");
                    app.SetDeviceState(kDeviceStateIdle);
                } else if (state == kDeviceStateSpeaking) {
                    ESP_LOGI(TAG, "Voice PE mute switch is active; current TTS playback continues");
                }
            } else if (state == kDeviceStateIdle || state == kDeviceStateSpeaking) {
                audio_service.EnableWakeWordDetection(true);
            }
        });
    }

    void InitializeKnob() {
        knob_ = std::make_unique<Knob>(VOICE_PE_ENCODER_A_GPIO, VOICE_PE_ENCODER_B_GPIO);
        knob_->OnRotate([this](bool clockwise) {
            ESP_LOGD(TAG, "Knob rotation detected. Clockwise:%s", clockwise ? "true" : "false");
            Application::GetInstance().Schedule([this, clockwise]() {
                auto* codec = GetAudioCodec();
                auto current_volume = codec->output_volume();
                auto volume = std::clamp(
                    current_volume + (clockwise ? VOICE_PE_VOLUME_STEP : -VOICE_PE_VOLUME_STEP),
                    0,
                    100);
                if (volume == current_volume) {
                    return;
                }
                codec->SetOutputVolume(volume);
                ESP_LOGI(TAG, "Voice PE volume set to %d", volume);
            });
        });
    }

    void InitializeButtons() {
        center_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            ESP_LOGI(TAG, "Voice PE center button clicked");
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            if (muted_.load()) {
                ESP_LOGW(TAG, "Ignoring Voice PE chat toggle while mute switch is active");
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
        InitializeLed();
        InitializeMuteSwitch();
        InitializeJackDetect();
        InitializeInputPolling();
        InitializeKnob();
        InitializeButtons();
        ESP_LOGI(TAG, "Home Assistant Voice PE board initialized");
    }

    virtual ~HomeAssistantVoicePeBoard() {
        if (input_poll_timer_ != nullptr) {
            esp_timer_stop(input_poll_timer_);
            esp_timer_delete(input_poll_timer_);
        }
    }

    virtual AudioCodec* GetAudioCodec() override {
        static VoicePeAudioCodec audio_codec(internal_i2c_bus_);
        return &audio_codec;
    }

    virtual Led* GetLed() override {
        return led_strip_.get();
    }

    virtual bool IsMicrophoneMuted() override {
        return muted_.load();
    }

    virtual bool ShouldUploadAudioDuringSpeaking() override {
        return false;
    }
};

DECLARE_BOARD(HomeAssistantVoicePeBoard);
