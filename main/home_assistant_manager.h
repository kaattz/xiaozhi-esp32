#ifndef HOME_ASSISTANT_MANAGER_H
#define HOME_ASSISTANT_MANAGER_H

#include "ha_mqtt_settings.h"

#include <memory>
#include <mutex>
#include <string>

#include <MQTTRemote.h>
#include <HaBridge.h>
#include <entities/HaEntityButton.h>
#include <entities/HaEntityNumber.h>
#include <entities/HaEntityString.h>
#include <entities/HaEntityText.h>
#include <nlohmann/json.hpp>

class HomeAssistantManager {
public:
    static HomeAssistantManager& GetInstance() {
        static HomeAssistantManager instance;
        return instance;
    }

    HomeAssistantManager(const HomeAssistantManager&) = delete;
    HomeAssistantManager& operator=(const HomeAssistantManager&) = delete;

    void Start();
    void PublishDeviceState(const std::string& state);
    void PublishUserMessage(const std::string& message);
    void PublishAssistantMessage(const std::string& message);
    void PublishVolume(int volume);
    void PublishBrightness(int brightness);

    HaEntityButton* GetWakeUpButtonEntity();
    HaEntityString* GetDeviceStatusEntity();
    HaEntityString* GetUserMessageEntity();
    HaEntityString* GetAssistantMessageEntity();
    HaEntityText* GetAnnouncementEntity();
    HaEntityText* GetQuestionEntity();
    HaEntityNumber* GetBrightnessEntity();
    HaEntityNumber* GetVolumeEntity();

private:
    HomeAssistantManager() = default;
    ~HomeAssistantManager() = default;

    void SetupJsonForThisDevice();
    void SetupEntities();
    void PublishConfiguration();
    bool CanPublish() const;

    mutable std::mutex mutex_;
    bool started_ = false;
    bool connected_ = false;
    HaMqttSettings settings_;
    nlohmann::json json_this_device_doc_;
    std::unique_ptr<MQTTRemote> mqtt_remote_;
    std::unique_ptr<HaBridge> ha_bridge_;
    std::unique_ptr<HaEntityButton> wake_up_button_;
    std::unique_ptr<HaEntityString> device_status_;
    std::unique_ptr<HaEntityString> user_message_;
    std::unique_ptr<HaEntityString> assistant_message_;
    std::unique_ptr<HaEntityText> announcement_;
    std::unique_ptr<HaEntityText> question_;
    std::unique_ptr<HaEntityNumber> brightness_;
    std::unique_ptr<HaEntityNumber> volume_;
};

#endif // HOME_ASSISTANT_MANAGER_H
