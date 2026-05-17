#include "voice_pe_audio_codec.h"
#include "config.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <vector>

#define TAG "VoicePeAudioCodec"

namespace {
constexpr int kMicSlotCount = 2;
constexpr int kWakeWordMicSlot = 1;
constexpr int kVoiceMicSlot = 1;
constexpr int kMicSampleShift = 8;
constexpr int kMicGainNumerator = 3;
constexpr int kMicGainDenominator = 2;
constexpr int kOutputSlotCount = 2;
constexpr int kReferenceRingCapacitySamples = 3200;
constexpr size_t kReferencePlaybackDelaySamples = 0;
constexpr int64_t kReferenceStaleAfterUs = 300 * 1000;
constexpr int64_t kReferenceUnderrunLogWindowUs = 2000 * 1000;
constexpr int64_t kOutputProbeIntervalUs = 2000 * 1000;
constexpr int kToneSampleRate = AUDIO_OUTPUT_SAMPLE_RATE;
constexpr int kToneFrequency = 1000;
constexpr uint32_t kMicReadTimeoutMs = 200;
constexpr float kPi = 3.14159265358979323846f;

void AbortOnAudioEffectError(esp_ae_err_t err, const char* action) {
    if (err != ESP_AE_ERR_OK) {
        ESP_LOGE(TAG, "%s failed: %d", action, static_cast<int>(err));
        abort();
    }
}

int16_t ScaleReferenceSample(int16_t sample, int volume) {
    int32_t value = static_cast<int32_t>(sample) * std::clamp(volume, 0, 100) / 100;
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return static_cast<int16_t>(value);
}
} // namespace

VoicePeAudioCodec::VoicePeAudioCodec(i2c_master_bus_handle_t i2c_bus)
    : dac_(i2c_bus) {
    duplex_ = AUDIO_INPUT_REFERENCE;
    input_reference_ = AUDIO_INPUT_REFERENCE;
    input_sample_rate_ = AUDIO_INPUT_SAMPLE_RATE;
    output_sample_rate_ = AUDIO_OUTPUT_SAMPLE_RATE;
    input_channels_ = AUDIO_INPUT_REFERENCE ? 2 : 1;
    output_channels_ = 1;

    InitializeReferenceResampler();
    InitializeAmp();
    InitializeOutputI2s();
    InitializeInputI2s();
    ESP_LOGI(TAG, "Voice PE audio codec initialized: input=%dHz output=%dHz channels=%d reference=%s",
        input_sample_rate_, output_sample_rate_, input_channels_, input_reference_ ? "true" : "false");
}

VoicePeAudioCodec::~VoicePeAudioCodec() {
    if (reference_resampler_ != nullptr) {
        esp_ae_rate_cvt_close(reference_resampler_);
    }
    if (rx_handle_ != nullptr) {
        if (input_enabled_) {
            ESP_ERROR_CHECK(i2s_channel_disable(rx_handle_));
        }
        ESP_ERROR_CHECK(i2s_del_channel(rx_handle_));
    }
    if (tx_handle_ != nullptr) {
        if (output_enabled_) {
            ESP_ERROR_CHECK(i2s_channel_disable(tx_handle_));
        }
        ESP_ERROR_CHECK(i2s_del_channel(tx_handle_));
    }
}

void VoicePeAudioCodec::InitializeAmp() {
    gpio_config_t amp_cfg = {
        .pin_bit_mask = 1ULL << VOICE_PE_AMP_ENABLE_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&amp_cfg));
    ESP_ERROR_CHECK(gpio_set_level(VOICE_PE_AMP_ENABLE_GPIO, 0));
}

void VoicePeAudioCodec::InitializeOutputI2s() {
    // Voice PE ESPHome config uses i2s_mode: secondary; external audio hardware drives BCLK/WS.
    i2s_chan_config_t tx_chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_SLAVE,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_handle_, nullptr));

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
#ifdef I2S_HW_VERSION_2
            .ext_clk_freq_hz = 0,
#endif
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
#ifdef I2S_HW_VERSION_2
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
#endif
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = VOICE_PE_SPK_BCLK_GPIO,
            .ws = VOICE_PE_SPK_LRCLK_GPIO,
            .dout = VOICE_PE_SPK_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &tx_std_cfg));
}

void VoicePeAudioCodec::InitializeInputI2s() {
    // Voice PE ESPHome config uses i2s_mode: secondary; external audio hardware drives BCLK/WS.
    i2s_chan_config_t rx_chan_cfg = {
        .id = I2S_NUM_1,
        .role = I2S_ROLE_SLAVE,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, nullptr, &rx_handle_));

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)input_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
#ifdef I2S_HW_VERSION_2
            .ext_clk_freq_hz = 0,
#endif
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
#ifdef I2S_HW_VERSION_2
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
#endif
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = VOICE_PE_MIC_BCLK_GPIO,
            .ws = VOICE_PE_MIC_LRCLK_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = VOICE_PE_MIC_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &rx_std_cfg));
}

void VoicePeAudioCodec::InitializeReferenceResampler() {
    if (!input_reference_) {
        return;
    }
    if (output_sample_rate_ == input_sample_rate_) {
        return;
    }

    esp_ae_rate_cvt_cfg_t reference_resampler_cfg = {
        .src_rate = (uint32_t)output_sample_rate_,
        .dest_rate = (uint32_t)input_sample_rate_,
        .channel = 1,
        .bits_per_sample = 16,
        .complexity = 2,
        .perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED,
    };
    auto ret = esp_ae_rate_cvt_open(&reference_resampler_cfg, &reference_resampler_);
    AbortOnAudioEffectError(ret, "Open reference resampler");
    if (reference_resampler_ == nullptr) {
        ESP_LOGE(TAG, "Open reference resampler returned null handle");
        abort();
    }
}

int16_t VoicePeAudioCodec::SaturateMicSample(int32_t sample) {
    int64_t value = sample >> kMicSampleShift;
    value = value * kMicGainNumerator / kMicGainDenominator;
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return static_cast<int16_t>(value);
}

int VoicePeAudioCodec::Read(int16_t* dest, int samples) {
    std::lock_guard<std::mutex> lock(input_mutex_);
    if (!input_enabled_) {
        ESP_LOGE(TAG, "Read called while input is disabled");
        return 0;
    }

    int frames_requested = input_reference_ ? samples / input_channels_ : samples;
    std::vector<int32_t> bit32_buffer(frames_requested * kMicSlotCount);
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(rx_handle_, bit32_buffer.data(),
        bit32_buffer.size() * sizeof(int32_t), &bytes_read, kMicReadTimeoutMs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S RX read failed: %s, requested=%u bytes, read=%u bytes",
            esp_err_to_name(err),
            static_cast<unsigned>(bit32_buffer.size() * sizeof(int32_t)),
            static_cast<unsigned>(bytes_read));
        return 0;
    }

    int frames = bytes_read / (sizeof(int32_t) * kMicSlotCount);
    frames = std::min(frames, frames_requested);
    std::vector<int16_t> mic_data(frames);
    for (int i = 0; i < frames; ++i) {
        mic_data[i] = SaturateMicSample(bit32_buffer[i * kMicSlotCount + selected_mic_slot_]);
    }
    LogMicProbe(mic_data.data(), frames);

    if (input_reference_) {
        std::vector<int16_t> reference_data(frames);
        PopReferenceSamples(reference_data.data(), frames);
        for (int i = 0; i < frames; ++i) {
            dest[i * input_channels_] = mic_data[i];
            dest[i * input_channels_ + 1] = reference_data[i];
        }
        return frames * input_channels_;
    }

    std::copy(mic_data.begin(), mic_data.end(), dest);
    return frames;
}

int VoicePeAudioCodec::Write(const int16_t* data, int samples) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    if (!output_enabled_) {
        ESP_LOGE(TAG, "Write called while output is disabled");
        return 0;
    }

    std::vector<int32_t> bit32_buffer(samples * kOutputSlotCount);
    for (int i = 0; i < samples; ++i) {
        int32_t sample = static_cast<int32_t>(data[i]) << 16;
        bit32_buffer[i * kOutputSlotCount] = sample;
        bit32_buffer[i * kOutputSlotCount + 1] = sample;
    }

    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(tx_handle_, bit32_buffer.data(),
        bit32_buffer.size() * sizeof(int32_t), &bytes_written, portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S TX write failed: %s", esp_err_to_name(err));
        return 0;
    }
    int samples_written = bytes_written / (sizeof(int32_t) * kOutputSlotCount);
    if (samples_written < samples) {
        ESP_LOGW(TAG, "I2S TX partial write: requested=%d written=%d", samples, samples_written);
    }
    ESP_LOGD(TAG, "I2S TX write: requested=%d written=%d bytes=%u",
        samples,
        samples_written,
        static_cast<unsigned>(bytes_written));
    LogOutputProbe(data, samples_written);
    AppendReferenceSamples(data, samples_written);
    return samples_written;
}

void VoicePeAudioCodec::LogOutputProbe(const int16_t* data, int samples) {
    if (data == nullptr || samples <= 0) {
        return;
    }

    for (int i = 0; i < samples; ++i) {
        int32_t sample = data[i];
        int abs_sample = sample == INT16_MIN ? 32768 : std::abs(sample);
        output_probe_peak_ = std::max(output_probe_peak_, abs_sample);
        output_probe_sum_squares_ += static_cast<int64_t>(sample) * sample;
    }
    output_probe_samples_ += samples;

    const int64_t now_us = esp_timer_get_time();
    if (now_us - last_output_probe_log_us_ < kOutputProbeIntervalUs) {
        return;
    }

    float rms = output_probe_samples_ > 0
        ? std::sqrt(static_cast<float>(output_probe_sum_squares_) / output_probe_samples_)
        : 0.0f;
    ESP_LOGI(TAG, "output probe: samples=%d peak=%d rms=%.1f volume=%d",
        output_probe_samples_, output_probe_peak_, rms, output_volume_);

    output_probe_peak_ = 0;
    output_probe_sum_squares_ = 0;
    output_probe_samples_ = 0;
    last_output_probe_log_us_ = now_us;
}

void VoicePeAudioCodec::EnableInput(bool enable) {
    std::lock_guard<std::mutex> lock(input_mutex_);
    if (enable == input_enabled_) {
        return;
    }
    if (enable) {
        ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
    } else {
        ESP_ERROR_CHECK(i2s_channel_disable(rx_handle_));
    }
    AudioCodec::EnableInput(enable);
}

void VoicePeAudioCodec::EnableOutput(bool enable) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    if (enable == output_enabled_) {
        return;
    }
    if (enable) {
        if (!dac_initialized_) {
            ESP_ERROR_CHECK(dac_.Initialize());
            dac_initialized_ = true;
        }
        ESP_ERROR_CHECK(dac_.SetVolume(output_volume_));
        ESP_ERROR_CHECK(dac_.SetMuted(false));
        ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
        ESP_ERROR_CHECK(gpio_set_level(VOICE_PE_AMP_ENABLE_GPIO, 1));
    } else {
        ESP_ERROR_CHECK(gpio_set_level(VOICE_PE_AMP_ENABLE_GPIO, 0));
        ESP_ERROR_CHECK(dac_.SetMuted(true));
        ESP_ERROR_CHECK(i2s_channel_disable(tx_handle_));
    }
    AudioCodec::EnableOutput(enable);
}

void VoicePeAudioCodec::SetOutputVolume(int volume) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    AudioCodec::SetOutputVolume(volume);
    if (dac_initialized_) {
        ESP_ERROR_CHECK(dac_.SetVolume(output_volume_));
    }
    Board::GetInstance().GetLed()->OnStateChanged();
}

void VoicePeAudioCodec::SetInputPurpose(AudioInputPurpose purpose) {
    std::lock_guard<std::mutex> lock(input_mutex_);
    switch (purpose) {
        case AudioInputPurpose::kWakeWord:
            selected_mic_slot_ = kWakeWordMicSlot;
            break;
        case AudioInputPurpose::kVoiceProcessing:
        case AudioInputPurpose::kAudioTesting:
            selected_mic_slot_ = kVoiceMicSlot;
            break;
        default:
            break;
    }
}

void VoicePeAudioCodec::AppendReferenceSamples(const int16_t* data, int samples) {
    if (!input_reference_ || samples <= 0) {
        return;
    }

    std::vector<int16_t> scaled(samples);
    for (int i = 0; i < samples; ++i) {
        scaled[i] = ScaleReferenceSample(data[i], output_volume_);
    }

    std::vector<int16_t> reference_data;
    if (reference_resampler_ != nullptr) {
        uint32_t output_samples = 0;
        auto ret = esp_ae_rate_cvt_get_max_out_sample_num(reference_resampler_, samples, &output_samples);
        AbortOnAudioEffectError(ret, "Get reference resampler output size");

        reference_data.resize(output_samples);
        uint32_t actual_output = output_samples;
        ret = esp_ae_rate_cvt_process(reference_resampler_, (esp_ae_sample_t)scaled.data(), samples,
            (esp_ae_sample_t)reference_data.data(), &actual_output);
        AbortOnAudioEffectError(ret, "Process reference resampler");
        reference_data.resize(actual_output);
    } else {
        reference_data = std::move(scaled);
    }

    LogReferenceProbe(reference_data.data(), reference_data.size());

    const int64_t now_us = esp_timer_get_time();
    std::lock_guard<std::mutex> lock(reference_mutex_);
    if (last_reference_append_us_ != 0 && now_us - last_reference_append_us_ > kReferenceStaleAfterUs) {
        reference_ring_buffer_.clear();
        reference_underrun_samples_ = 0;
    }
    if (reference_ring_buffer_.size() + reference_data.size() > kReferenceRingCapacitySamples) {
        if (now_us - last_reference_overflow_log_us_ > 2000000) {
            ESP_LOGW(TAG, "reference overflow: buffered=%u incoming=%u capacity=%d",
                static_cast<unsigned>(reference_ring_buffer_.size()),
                static_cast<unsigned>(reference_data.size()),
                kReferenceRingCapacitySamples);
            last_reference_overflow_log_us_ = now_us;
        }
    }
    while (reference_ring_buffer_.size() + reference_data.size() > kReferenceRingCapacitySamples) {
        if (reference_ring_buffer_.empty()) {
            reference_data.erase(reference_data.begin(),
                reference_data.begin() + (reference_data.size() - kReferenceRingCapacitySamples));
            break;
        }
        reference_ring_buffer_.pop_front();
    }
    reference_ring_buffer_.insert(reference_ring_buffer_.end(), reference_data.begin(), reference_data.end());
    last_reference_append_us_ = esp_timer_get_time();
}

void VoicePeAudioCodec::PopReferenceSamples(int16_t* dest, int samples) {
    std::lock_guard<std::mutex> lock(reference_mutex_);
    int underrun = 0;
    const int64_t now_us = esp_timer_get_time();
    if (last_reference_append_us_ != 0 && now_us - last_reference_append_us_ > kReferenceStaleAfterUs) {
        reference_ring_buffer_.clear();
        reference_underrun_samples_ = 0;
        last_reference_append_us_ = 0;
    }

    size_t available = 0;
    if (reference_ring_buffer_.size() > kReferencePlaybackDelaySamples) {
        available = reference_ring_buffer_.size() - kReferencePlaybackDelaySamples;
    }

    for (int i = 0; i < samples; ++i) {
        if (available == 0) {
            dest[i] = 0;
            underrun++;
            continue;
        }
        dest[i] = reference_ring_buffer_.front();
        reference_ring_buffer_.pop_front();
        available--;
    }
    if (last_reference_append_us_ == 0 || now_us - last_reference_append_us_ > kReferenceUnderrunLogWindowUs) {
        reference_underrun_samples_ = 0;
        return;
    }

    reference_underrun_samples_ += underrun;
    if (reference_underrun_samples_ > input_sample_rate_) {
        ESP_LOGW(TAG, "reference underrun: samples=%d", reference_underrun_samples_);
        reference_underrun_samples_ = 0;
    }
}

void VoicePeAudioCodec::PlayTestTone(int duration_ms) {
    const bool was_enabled = output_enabled_;
    if (!was_enabled) {
        EnableOutput(true);
    }

    constexpr int chunk_samples = kToneSampleRate / 100;
    const int total_samples = kToneSampleRate * duration_ms / 1000;
    std::vector<int16_t> chunk(chunk_samples);
    int generated = 0;
    while (generated < total_samples) {
        const int count = std::min(chunk_samples, total_samples - generated);
        for (int i = 0; i < count; ++i) {
            float phase = 2.0f * kPi * kToneFrequency * (generated + i) / kToneSampleRate;
            chunk[i] = static_cast<int16_t>(std::sin(phase) * 12000);
        }
        Write(chunk.data(), count);
        generated += count;
    }

    if (!was_enabled) {
        EnableOutput(false);
    }
}

void VoicePeAudioCodec::LogMicProbe(const int16_t* data, int samples) {
    for (int i = 0; i < samples; ++i) {
        mic_probe_sum_squares_ += static_cast<int64_t>(data[i]) * data[i];
    }
    mic_probe_samples_ += samples;

    const int64_t now_us = esp_timer_get_time();
    if (mic_probe_samples_ == 0 || now_us - last_mic_probe_log_us_ < 2000000) {
        return;
    }

    const double mean_square = static_cast<double>(mic_probe_sum_squares_) / mic_probe_samples_;
    ESP_LOGD(TAG, "mic probe: selected_slot=%d samples=%d rms=%.1f",
        selected_mic_slot_, mic_probe_samples_, std::sqrt(mean_square));

    mic_probe_sum_squares_ = 0;
    mic_probe_samples_ = 0;
    last_mic_probe_log_us_ = now_us;
}

void VoicePeAudioCodec::LogReferenceProbe(const int16_t* data, int samples) {
    for (int i = 0; i < samples; ++i) {
        reference_probe_sum_squares_ += static_cast<int64_t>(data[i]) * data[i];
    }
    reference_probe_samples_ += samples;

    const int64_t now_us = esp_timer_get_time();
    if (reference_probe_samples_ == 0 || now_us - last_reference_probe_log_us_ < 2000000) {
        return;
    }

    const double mean_square = static_cast<double>(reference_probe_sum_squares_) / reference_probe_samples_;
    ESP_LOGI(TAG, "reference probe: samples=%d rms=%.1f",
        reference_probe_samples_,
        std::sqrt(mean_square));

    reference_probe_sum_squares_ = 0;
    reference_probe_samples_ = 0;
    last_reference_probe_log_us_ = now_us;
}
