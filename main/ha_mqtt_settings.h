#ifndef HA_MQTT_SETTINGS_H
#define HA_MQTT_SETTINGS_H

#include <string>

class HaMqttSettings {
public:
    bool enabled = false;
    std::string host;
    int port = 1883;
    std::string username;
    std::string password;
    std::string client_id;
    std::string device_name;
    std::string model;
    std::string manufacturer;

    bool IsValid() const;

    static HaMqttSettings Load();
};

#endif // HA_MQTT_SETTINGS_H
