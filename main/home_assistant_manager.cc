#include "home_assistant_manager.h"

#include "application.h"
#include "audio_codec.h"
#include "backlight.h"
#include "board.h"

#include <algorithm>

#include <esp_log.h>

#define TAG "HomeAssistantManager"

namespace {
int ClampPercent(float value) {
    return std::clamp(static_cast<int>(value), 0, 100);
}
} // namespace

void HomeAssistantManager::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_) {
        return;
    }

    settings_ = HaMqttSettings::Load();
    if (!settings_.IsValid()) {
        ESP_LOGI(TAG, "HA MQTT disabled or not configured");
        return;
    }

    SetupJsonForThisDevice();

    mqtt_remote_.reset(new MQTTRemote(settings_.client_id,
                                      settings_.host,
                                      settings_.port,
                                      settings_.username,
                                      settings_.password,
                                      {.rx_buffer_size = 256, .tx_buffer_size = 1024, .keep_alive_s = 10}));
    ha_bridge_.reset(new HaBridge(*mqtt_remote_, settings_.device_name, json_this_device_doc_));
    SetupEntities();

    auto& board = Board::GetInstance();
    auto backlight = board.GetBacklight();
    auto codec = board.GetAudioCodec();

    wake_up_button_->setOnPressed([]() {
        Application::GetInstance().Schedule([]() {
            Application::GetInstance().WakeWordInvoke("你好小智");
        });
    });

    announcement_->setOnText([](std::string text) {
        if (text.empty()) {
            return;
        }
        Application::GetInstance().PlayRemoteAnnouncement(text);
    });

    if (backlight != nullptr) {
        brightness_->setOnNumber([backlight](float number) {
            const auto brightness = ClampPercent(number);
            backlight->SetBrightness(brightness, true);
            HomeAssistantManager::GetInstance().PublishBrightness(brightness);
        });
    }

    if (codec != nullptr) {
        volume_->setOnNumber([codec](float number) {
            const auto volume = ClampPercent(number);
            codec->SetOutputVolume(volume);
            HomeAssistantManager::GetInstance().PublishVolume(volume);
        });
    }

    mqtt_remote_->start([this, backlight, codec](bool connected) {
        std::lock_guard<std::mutex> lock(mutex_);
        connected_ = connected;
        if (!connected_) {
            return;
        }

        PublishConfiguration();
        if (backlight != nullptr) {
            brightness_->publishNumber(backlight->brightness());
        }
        if (codec != nullptr) {
            volume_->publishNumber(codec->output_volume());
        }
    });

    started_ = true;
    ESP_LOGI(TAG, "HA MQTT started: %s:%d", settings_.host.c_str(), settings_.port);
}

void HomeAssistantManager::PublishDeviceState(const std::string& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (CanPublish()) {
        device_status_->publishString(state);
    }
}

void HomeAssistantManager::PublishUserMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (CanPublish()) {
        user_message_->publishString(message);
    }
}

void HomeAssistantManager::PublishAssistantMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (CanPublish()) {
        assistant_message_->publishString(message);
    }
}

void HomeAssistantManager::PublishVolume(int volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (CanPublish() && volume_ != nullptr) {
        volume_->publishNumber(std::clamp(volume, 0, 100));
    }
}

void HomeAssistantManager::PublishBrightness(int brightness) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (CanPublish() && brightness_ != nullptr) {
        brightness_->publishNumber(std::clamp(brightness, 0, 100));
    }
}

HaEntityButton* HomeAssistantManager::GetWakeUpButtonEntity() {
    return wake_up_button_.get();
}

HaEntityString* HomeAssistantManager::GetDeviceStatusEntity() {
    return device_status_.get();
}

HaEntityString* HomeAssistantManager::GetUserMessageEntity() {
    return user_message_.get();
}

HaEntityString* HomeAssistantManager::GetAssistantMessageEntity() {
    return assistant_message_.get();
}

HaEntityText* HomeAssistantManager::GetAnnouncementEntity() {
    return announcement_.get();
}

HaEntityNumber* HomeAssistantManager::GetBrightnessEntity() {
    return brightness_.get();
}

HaEntityNumber* HomeAssistantManager::GetVolumeEntity() {
    return volume_.get();
}

void HomeAssistantManager::SetupJsonForThisDevice() {
    json_this_device_doc_.clear();
    json_this_device_doc_["identifiers"] = settings_.model + "_" + settings_.client_id;
    json_this_device_doc_["name"] = settings_.device_name;
    json_this_device_doc_["sw_version"] = "1.0.0";
    json_this_device_doc_["model"] = settings_.model;
    json_this_device_doc_["manufacturer"] = settings_.manufacturer;
}

void HomeAssistantManager::SetupEntities() {
    wake_up_button_.reset(new HaEntityButton(*ha_bridge_, "wake up", ""));
    device_status_.reset(new HaEntityString(*ha_bridge_, "状态", "device_status",
                                            {.with_attributes = true, .force_update = false}));
    user_message_.reset(new HaEntityString(*ha_bridge_, "user", "user_message",
                                           {.with_attributes = true, .force_update = false}));
    assistant_message_.reset(new HaEntityString(*ha_bridge_, "assistant", "assistant_message",
                                                {.with_attributes = true, .force_update = false}));
    announcement_.reset(new HaEntityText(*ha_bridge_, "Announcement", "announcement",
                                         {.min_text_length = 0,
                                          .max_text_length = 255,
                                          .with_state_topic = false,
                                          .is_password = false,
                                          .force_update = false,
                                          .retain = false}));
    brightness_.reset(new HaEntityNumber(*ha_bridge_, "亮度", "brightness",
                                         {.min_value = 0,
                                          .max_value = 100,
                                          .unit = "",
                                          .device_class = "",
                                          .force_update = false,
                                          .retain = false}));
    volume_.reset(new HaEntityNumber(*ha_bridge_, "音量", "volume",
                                     {.min_value = 0,
                                      .max_value = 100,
                                      .unit = "",
                                      .device_class = "",
                                      .force_update = false,
                                      .retain = false}));
}

void HomeAssistantManager::PublishConfiguration() {
    wake_up_button_->publishConfiguration();
    device_status_->publishConfiguration();
    user_message_->publishConfiguration();
    assistant_message_->publishConfiguration();
    announcement_->publishConfiguration();
    brightness_->publishConfiguration();
    volume_->publishConfiguration();
}

bool HomeAssistantManager::CanPublish() const {
    return started_ && connected_ && device_status_ != nullptr;
}
