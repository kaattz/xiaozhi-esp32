#ifndef _AIC3204_AUDIO_DAC_H_
#define _AIC3204_AUDIO_DAC_H_

#include <driver/i2c_master.h>
#include <esp_err.h>

#include <cstdint>

class Aic3204AudioDac {
public:
    explicit Aic3204AudioDac(i2c_master_bus_handle_t i2c_bus);

    esp_err_t Initialize();
    esp_err_t SetMuted(bool muted);
    esp_err_t SetVolume(int volume);

private:
    i2c_master_dev_handle_t i2c_device_ = nullptr;
    bool initialized_ = false;
    bool muted_ = true;
    int volume_ = 70;

    esp_err_t WriteReg(uint8_t reg, uint8_t value);
    esp_err_t SelectPage(uint8_t page);
    uint8_t VolumeToRegisterValue(int volume) const;
};

#endif // _AIC3204_AUDIO_DAC_H_
