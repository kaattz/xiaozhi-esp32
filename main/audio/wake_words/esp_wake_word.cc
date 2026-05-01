#include "esp_wake_word.h"
#include <esp_log.h>

#include <cmath>
#include <limits>

#define TAG "EspWakeWord"

namespace {
constexpr size_t kWakeRmsMaxSamples = 16000 * 2;

float RmsStatsToDbfs(double sum_squares, size_t sample_count) {
    if (sample_count == 0) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    auto rms = std::sqrt(sum_squares / sample_count);
    if (!std::isfinite(rms)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    if (rms < 1.0) {
        rms = 1.0;
    }
    return static_cast<float>(20.0 * std::log10(rms / 32768.0));
}
}

EspWakeWord::EspWakeWord() {
}

EspWakeWord::~EspWakeWord() {
    if (wakenet_data_ != nullptr) {
        wakenet_iface_->destroy(wakenet_data_);
        esp_srmodel_deinit(wakenet_model_);
    }
}

bool EspWakeWord::Initialize(AudioCodec* codec, srmodel_list_t* models_list) {
    codec_ = codec;

    if (models_list == nullptr) {
        wakenet_model_ = esp_srmodel_init("model");
    } else {
        wakenet_model_ = models_list;
    }

    if (wakenet_model_ == nullptr || wakenet_model_->num == -1) {
        ESP_LOGE(TAG, "Failed to initialize wakenet model");
        return false;
    }
    if(wakenet_model_->num > 1) {
        ESP_LOGW(TAG, "More than one model found, using the first one");
    } else if (wakenet_model_->num == 0) {
        ESP_LOGE(TAG, "No model found");
        return false;
    }
    char *model_name = wakenet_model_->model_name[0];
    wakenet_iface_ = (esp_wn_iface_t*)esp_wn_handle_from_name(model_name);
    wakenet_data_ = wakenet_iface_->create(model_name, DET_MODE_95);

    int frequency = wakenet_iface_->get_samp_rate(wakenet_data_);
    int audio_chunksize = wakenet_iface_->get_samp_chunksize(wakenet_data_);
    ESP_LOGI(TAG, "Wake word(%s),freq: %d, chunksize: %d", model_name, frequency, audio_chunksize);

    return true;
}

void EspWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void EspWakeWord::Start() {
    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    input_buffer_.clear();
    ResetWakeRmsLocked();
    running_ = true;
}

void EspWakeWord::Stop() {
    running_ = false;

    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    input_buffer_.clear();
}

void EspWakeWord::Feed(const std::vector<int16_t>& data) {
    if (wakenet_data_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    // Check running state inside lock to avoid TOCTOU race with Stop()
    if (!running_) {
        return;
    }

    if (codec_->input_channels() == 2) {
        std::vector<int16_t> mono_data;
        mono_data.reserve(data.size() / 2);
        for (size_t i = 0; i < data.size(); i += 2) {
            mono_data.push_back(data[i]);
        }
        input_buffer_.insert(input_buffer_.end(), mono_data.begin(), mono_data.end());
        AddWakeRmsSamplesLocked(mono_data.data(), mono_data.size());
    } else {
        input_buffer_.insert(input_buffer_.end(), data.begin(), data.end());
        AddWakeRmsSamplesLocked(data.data(), data.size());
    }

    int chunksize = wakenet_iface_->get_samp_chunksize(wakenet_data_);
    while (input_buffer_.size() >= chunksize) {
        int res = wakenet_iface_->detect(wakenet_data_, input_buffer_.data());
        if (res > 0) {
            last_detected_wake_word_ = wakenet_iface_->get_word_name(wakenet_data_, res);
            running_ = false;
            input_buffer_.clear();

            if (wake_word_detected_callback_) {
                wake_word_detected_callback_(last_detected_wake_word_);
            }
            break;
        }
        input_buffer_.erase(input_buffer_.begin(), input_buffer_.begin() + chunksize);
    }
}

size_t EspWakeWord::GetFeedSize() {
    if (wakenet_data_ == nullptr) {
        return 0;
    }
    return wakenet_iface_->get_samp_chunksize(wakenet_data_);
}

void EspWakeWord::EncodeWakeWordData() {
}

bool EspWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    return false;
}

float EspWakeWord::GetLastWakeRmsDbfs() const {
    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    return RmsStatsToDbfs(wake_rms_sum_squares_, wake_rms_sample_count_);
}

void EspWakeWord::ResetWakeRmsLocked() {
    wake_rms_chunks_.clear();
    wake_rms_sum_squares_ = 0.0;
    wake_rms_sample_count_ = 0;
}

void EspWakeWord::AddWakeRmsSamplesLocked(const int16_t* data, size_t sample_count) {
    if (data == nullptr || sample_count == 0) {
        return;
    }

    double sum_squares = 0.0;
    for (size_t i = 0; i < sample_count; ++i) {
        auto sample = static_cast<double>(data[i]);
        sum_squares += sample * sample;
    }

    EspWakeWord::RmsWindowChunk chunk;
    chunk.sum_squares = sum_squares;
    chunk.sample_count = sample_count;
    wake_rms_chunks_.push_back(chunk);
    wake_rms_sum_squares_ += sum_squares;
    wake_rms_sample_count_ += sample_count;

    while (
        wake_rms_sample_count_ > kWakeRmsMaxSamples &&
        !wake_rms_chunks_.empty()
    ) {
        const auto& oldest = wake_rms_chunks_.front();
        wake_rms_sum_squares_ -= oldest.sum_squares;
        wake_rms_sample_count_ -= oldest.sample_count;
        wake_rms_chunks_.pop_front();
    }
}
