#ifndef _VOICE_PE_XMOS_H_
#define _VOICE_PE_XMOS_H_

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>

#include <cstdint>

class VoicePeXmos {
public:
    struct Version {
        uint8_t major = 0;
        uint8_t minor = 0;
        uint8_t patch = 0;
    };

    VoicePeXmos(i2c_master_bus_handle_t i2c_bus, gpio_num_t reset_gpio);

    esp_err_t Initialize();
    esp_err_t ReadVersion(Version& version);
    esp_err_t WritePipelineStages();

private:
    i2c_master_dev_handle_t i2c_device_ = nullptr;
    gpio_num_t reset_gpio_;
    Version version_;

    esp_err_t Reset();
};

#endif // _VOICE_PE_XMOS_H_
