#ifndef _VOICE_PE_AUDIO_CODEC_H_
#define _VOICE_PE_AUDIO_CODEC_H_

#include "aic3204_audio_dac.h"
#include "audio_codec.h"
#include "esp_ae_rate_cvt.h"

#include <driver/i2c_master.h>

#include <cstdint>
#include <deque>
#include <mutex>

class VoicePeAudioCodec : public AudioCodec {
public:
    explicit VoicePeAudioCodec(i2c_master_bus_handle_t i2c_bus);
    virtual ~VoicePeAudioCodec();

    virtual void SetOutputVolume(int volume) override;
    virtual void EnableInput(bool enable) override;
    virtual void EnableOutput(bool enable) override;
    virtual void SetInputPurpose(AudioInputPurpose purpose) override;

    void PlayTestTone(int duration_ms);
    static int16_t SaturateMicSample(int32_t sample);

protected:
    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

private:
    Aic3204AudioDac dac_;
    std::mutex input_mutex_;
    std::mutex output_mutex_;
    std::mutex reference_mutex_;
    esp_ae_rate_cvt_handle_t reference_resampler_ = nullptr;
    std::deque<int16_t> reference_ring_buffer_;
    bool dac_initialized_ = false;
    // Guarded by input_mutex_; Read() and SetInputPurpose() must stay serialized.
    int selected_mic_slot_ = 1;
    int64_t mic_probe_sum_squares_ = 0;
    int mic_probe_samples_ = 0;
    int64_t last_mic_probe_log_us_ = 0;
    int64_t reference_probe_sum_squares_ = 0;
    int reference_probe_samples_ = 0;
    int reference_underrun_samples_ = 0;
    int64_t last_reference_append_us_ = 0;
    int64_t last_reference_probe_log_us_ = 0;
    int64_t last_reference_overflow_log_us_ = 0;
    int output_probe_peak_ = 0;
    int64_t output_probe_sum_squares_ = 0;
    int output_probe_samples_ = 0;
    int64_t last_output_probe_log_us_ = 0;

    void InitializeInputI2s();
    void InitializeOutputI2s();
    void InitializeAmp();
    void InitializeReferenceResampler();
    void AppendReferenceSamples(const int16_t* data, int samples);
    void PopReferenceSamples(int16_t* dest, int samples);
    void LogMicProbe(const int16_t* data, int samples);
    void LogReferenceProbe(const int16_t* data, int samples);
    void LogOutputProbe(const int16_t* data, int samples);
};

#endif // _VOICE_PE_AUDIO_CODEC_H_
