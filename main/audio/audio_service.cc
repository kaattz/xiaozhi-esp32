#include "audio_service.h"
#include <esp_log.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>

#define RATE_CVT_CFG(_src_rate, _dest_rate, _channel)        \
    (esp_ae_rate_cvt_cfg_t)                                  \
    {                                                        \
        .src_rate        = (uint32_t)(_src_rate),            \
        .dest_rate       = (uint32_t)(_dest_rate),           \
        .channel         = (uint8_t)(_channel),              \
        .bits_per_sample = ESP_AUDIO_BIT16,                  \
        .complexity      = 2,                                \
        .perf_type       = ESP_AE_RATE_CVT_PERF_TYPE_SPEED,  \
    }

#define OPUS_DEC_CFG(_sample_rate, _frame_duration_ms)                                                    \
    (esp_opus_dec_cfg_t)                                                                                  \
    {                                                                                                     \
        .sample_rate    = (uint32_t)(_sample_rate),                                                       \
        .channel        = ESP_AUDIO_MONO,                                                                 \
        .frame_duration = (esp_opus_dec_frame_duration_t)AS_OPUS_GET_FRAME_DRU_ENUM(_frame_duration_ms),  \
        .self_delimited = false,                                                                          \
    }

#if CONFIG_USE_AUDIO_PROCESSOR
#include "processors/afe_audio_processor.h"
#else
#include "processors/no_audio_processor.h"
#endif

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
#include "wake_words/afe_wake_word.h"
#include "wake_words/custom_wake_word.h"
#else
#include "wake_words/esp_wake_word.h"
#endif

#define TAG "AudioService"

namespace {
constexpr int kPlaybackTailGuardMs = 200;
constexpr int kDecodePacketIdleGuardMs = OPUS_FRAME_DURATION_MS * 2;
constexpr int64_t kVoicePipelineProbeIntervalUs = 2000 * 1000;

float CalculateRms(const std::vector<int16_t>& data) {
    if (data.empty()) {
        return 0.0f;
    }

    double sum_squares = 0.0;
    for (int16_t sample : data) {
        sum_squares += static_cast<double>(sample) * sample;
    }
    return static_cast<float>(std::sqrt(sum_squares / data.size()));
}

float CalculateMicRms(const std::vector<int16_t>& data, int channels) {
    if (data.empty()) {
        return 0.0f;
    }

    int stride = std::max(channels, 1);
    double sum_squares = 0.0;
    size_t sample_count = 0;
    for (size_t i = 0; i < data.size(); i += stride) {
        sum_squares += static_cast<double>(data[i]) * data[i];
        sample_count++;
    }

    if (sample_count == 0) {
        return 0.0f;
    }
    return static_cast<float>(std::sqrt(sum_squares / sample_count));
}
}

AudioService::AudioService() {
    event_group_ = xEventGroupCreate();
}

AudioService::~AudioService() {
    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
    }
    if (opus_encoder_ != nullptr) {
        esp_opus_enc_close(opus_encoder_);
    }
    if (opus_decoder_ != nullptr) {
        esp_opus_dec_close(opus_decoder_);
    }
    if (input_resampler_ != nullptr) {
        esp_ae_rate_cvt_close(input_resampler_);
    }
    if (output_resampler_ != nullptr) {
        esp_ae_rate_cvt_close(output_resampler_);
    }
}

void AudioService::Initialize(AudioCodec* codec) {
    codec_ = codec;
    codec_->Start();

    esp_opus_dec_cfg_t opus_dec_cfg = OPUS_DEC_CFG(codec->output_sample_rate(), OPUS_FRAME_DURATION_MS);
    auto ret = esp_opus_dec_open(&opus_dec_cfg, sizeof(esp_opus_dec_cfg_t), &opus_decoder_);
    if (opus_decoder_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create audio decoder, error code: %d", ret);
    } else {
        decoder_sample_rate_ = codec->output_sample_rate();
        decoder_duration_ms_ = OPUS_FRAME_DURATION_MS;
        decoder_frame_size_ = decoder_sample_rate_ / 1000 * OPUS_FRAME_DURATION_MS;
    }
    esp_opus_enc_config_t opus_enc_cfg = AS_OPUS_ENC_CONFIG();
    ret = esp_opus_enc_open(&opus_enc_cfg, sizeof(esp_opus_enc_config_t), &opus_encoder_);
    if (opus_encoder_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create audio encoder, error code: %d", ret);
    } else {
        encoder_sample_rate_ = 16000;
        encoder_duration_ms_ = OPUS_FRAME_DURATION_MS;
        esp_opus_enc_get_frame_size(opus_encoder_, &encoder_frame_size_, &encoder_outbuf_size_);
        encoder_frame_size_ = encoder_frame_size_ / sizeof(int16_t);
    }

    if (codec->input_sample_rate() != 16000) {
        esp_ae_rate_cvt_cfg_t input_resampler_cfg = RATE_CVT_CFG(
            codec->input_sample_rate(), ESP_AUDIO_SAMPLE_RATE_16K, codec->input_channels());
        auto resampler_ret = esp_ae_rate_cvt_open(&input_resampler_cfg, &input_resampler_);
        if (input_resampler_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create input resampler, error code: %d", resampler_ret);
        }
    }

#if CONFIG_USE_AUDIO_PROCESSOR
    audio_processor_ = std::make_unique<AfeAudioProcessor>();
#else
    audio_processor_ = std::make_unique<NoAudioProcessor>();
#endif

    audio_processor_->OnOutput([this](std::vector<int16_t>&& data) {
        debug_statistics_.processor_output_count++;
        debug_statistics_.processor_output_rms = CalculateRms(data);
        bool wait_for_encode_queue = !codec_->input_reference() && Board::GetInstance().ShouldUploadAudioDuringSpeaking();
        PushTaskToEncodeQueue(kAudioTaskTypeEncodeToSendQueue, std::move(data), wait_for_encode_queue);
    });

    audio_processor_->OnVadStateChange([this](bool speaking) {
        voice_detected_ = speaking;
        if (callbacks_.on_vad_change) {
            callbacks_.on_vad_change(speaking);
        }
    });

    esp_timer_create_args_t audio_power_timer_args = {
        .callback = [](void* arg) {
            AudioService* audio_service = (AudioService*)arg;
            audio_service->CheckAndUpdateAudioPowerState();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "audio_power_timer",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&audio_power_timer_args, &audio_power_timer_);
}

void AudioService::Start() {
    service_stopped_ = false;
    xEventGroupClearBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING | AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING);

    esp_timer_start_periodic(audio_power_timer_, 1000000);

#if CONFIG_USE_AUDIO_PROCESSOR
    /* Start the audio input task */
    xTaskCreatePinnedToCore([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->AudioInputTask();
        vTaskDelete(NULL);
    }, "audio_input", 2048 * 3, this, 8, &audio_input_task_handle_, 0);

    /* Start the audio output task */
    xTaskCreate([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->AudioOutputTask();
        vTaskDelete(NULL);
    }, "audio_output", 2048 * 2, this, 4, &audio_output_task_handle_);
#else
    /* Start the audio input task */
    xTaskCreate([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->AudioInputTask();
        vTaskDelete(NULL);
    }, "audio_input", 2048 * 2, this, 8, &audio_input_task_handle_);

    /* Start the audio output task */
    xTaskCreate([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->AudioOutputTask();
        vTaskDelete(NULL);
    }, "audio_output", 2048, this, 4, &audio_output_task_handle_);
#endif

    /* Start the opus codec task */
    xTaskCreate([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->OpusCodecTask();
        vTaskDelete(NULL);
    }, "opus_codec", 2048 * 12, this, 2, &opus_codec_task_handle_);
}

void AudioService::Stop() {
    esp_timer_stop(audio_power_timer_);
    service_stopped_ = true;
    xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING |
        AS_EVENT_WAKE_WORD_RUNNING |
        AS_EVENT_AUDIO_PROCESSOR_RUNNING);

    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    audio_encode_queue_.clear();
    audio_decode_queue_.clear();
    audio_playback_queue_.clear();
    audio_testing_queue_.clear();
    playback_buffering_ = false;
    playback_buffer_min_frames_ = 0;
    playback_buffer_timeout_ms_ = 0;
    playback_buffer_started_at_ = {};
    audio_queue_cv_.notify_all();
}

bool AudioService::ReadAudioData(std::vector<int16_t>& data, int sample_rate, int samples) {
    if (!codec_->input_enabled()) {
        esp_timer_stop(audio_power_timer_);
        esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
        codec_->EnableInput(true);
    }

    if (codec_->input_sample_rate() != sample_rate) {
        data.resize(samples * codec_->input_sample_rate() / sample_rate * codec_->input_channels());
        if (!codec_->InputData(data)) {
            return false;
        }
        if (input_resampler_ != nullptr) {
            std::lock_guard<std::mutex> lock(input_resampler_mutex_);
            uint32_t in_sample_num = data.size() / codec_->input_channels();
            uint32_t output_samples = 0;
            esp_ae_rate_cvt_get_max_out_sample_num(input_resampler_, in_sample_num, &output_samples);
            auto resampled = std::vector<int16_t>(output_samples * codec_->input_channels());
            uint32_t actual_output = output_samples;
            esp_ae_rate_cvt_process(input_resampler_, (esp_ae_sample_t)data.data(), in_sample_num,
                                   (esp_ae_sample_t)resampled.data(), &actual_output);
            resampled.resize(actual_output * codec_->input_channels());
            data = std::move(resampled);
        }
    } else {
        data.resize(samples * codec_->input_channels());
        if (!codec_->InputData(data)) {
            return false;
        }
    }

    /* Update the last input time */
    last_input_time_ = std::chrono::steady_clock::now();
    debug_statistics_.input_count++;

#if CONFIG_USE_AUDIO_DEBUGGER
    // 音频调试：发送原始音频数据
    if (audio_debugger_ == nullptr) {
        audio_debugger_ = std::make_unique<AudioDebugger>();
    }
    audio_debugger_->Feed(data);
#endif

    return true;
}

void AudioService::AudioInputTask() {
    while (true) {
        EventBits_t bits = xEventGroupWaitBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING |
            AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING,
            pdFALSE, pdFALSE, portMAX_DELAY);

        if (service_stopped_) {
            break;
        }
        if (audio_input_need_warmup_) {
            audio_input_need_warmup_ = false;
            vTaskDelay(pdMS_TO_TICKS(120));
            continue;
        }

        /* Used for audio testing in NetworkConfiguring mode by clicking the BOOT button */
        if (bits & AS_EVENT_AUDIO_TESTING_RUNNING) {
            if (audio_testing_queue_.size() >= AUDIO_TESTING_MAX_DURATION_MS / OPUS_FRAME_DURATION_MS) {
                ESP_LOGW(TAG, "Audio testing queue is full, stopping audio testing");
                EnableAudioTesting(false);
                continue;
            }
            std::vector<int16_t> data;
            int samples = OPUS_FRAME_DURATION_MS * 16000 / 1000;
            codec_->SetInputPurpose(AudioInputPurpose::kAudioTesting);
            if (ReadAudioData(data, 16000, samples)) {
                // If input channels is 2, we need to fetch the left channel data
                if (codec_->input_channels() == 2) {
                    auto mono_data = std::vector<int16_t>(data.size() / 2);
                    for (size_t i = 0, j = 0; i < mono_data.size(); ++i, j += 2) {
                        mono_data[i] = data[j];
                    }
                    data = std::move(mono_data);
                }
                PushTaskToEncodeQueue(kAudioTaskTypeEncodeToTestingQueue, std::move(data));
                continue;
            }
        }

        /* Feed the wake word and/or audio processor */
        if (bits & (AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING)) {
            int samples = 160; // 10ms
            std::vector<int16_t> data;
            if (bits & AS_EVENT_AUDIO_PROCESSOR_RUNNING) {
                codec_->SetInputPurpose(AudioInputPurpose::kVoiceProcessing);
            } else {
                codec_->SetInputPurpose(AudioInputPurpose::kWakeWord);
            }
            if (ReadAudioData(data, 16000, samples)) {
                debug_statistics_.raw_input_rms = CalculateMicRms(data, codec_->input_channels());
                if (bits & AS_EVENT_WAKE_WORD_RUNNING) {
                    wake_word_->Feed(data);
                }
                if (bits & AS_EVENT_AUDIO_PROCESSOR_RUNNING) {
                    debug_statistics_.processor_feed_count++;
                    audio_processor_->Feed(std::move(data));
                    LogVoicePipelineProbe();
                }
                continue;
            }
        }

        // Read timeout/error should not terminate the input task.
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGW(TAG, "Audio input task stopped");
}

void AudioService::AudioOutputTask() {
    while (true) {
        std::unique_lock<std::mutex> lock(audio_queue_mutex_);
        while (true) {
            audio_queue_cv_.wait(lock, [this]() { return !audio_playback_queue_.empty() || service_stopped_; });
            if (service_stopped_) {
                break;
            }
            if (playback_buffering_) {
                auto now = std::chrono::steady_clock::now();
                if (playback_buffer_started_at_.time_since_epoch().count() == 0) {
                    playback_buffer_started_at_ = now;
                }
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - playback_buffer_started_at_).count();
                if (audio_playback_queue_.size() < playback_buffer_min_frames_ &&
                    elapsed_ms < playback_buffer_timeout_ms_) {
                    auto wait_ms = playback_buffer_timeout_ms_ - static_cast<int>(elapsed_ms);
                    audio_queue_cv_.wait_for(lock, std::chrono::milliseconds(wait_ms));
                    continue;
                }
                ESP_LOGI(TAG, "Playback buffer release: frames=%u min=%u elapsed_ms=%d",
                    static_cast<unsigned>(audio_playback_queue_.size()),
                    static_cast<unsigned>(playback_buffer_min_frames_),
                    static_cast<int>(elapsed_ms));
                playback_buffering_ = false;
            }
            break;
        }
        if (service_stopped_) {
            break;
        }

        auto task = std::move(audio_playback_queue_.front());
        audio_playback_queue_.pop_front();
        playback_active_ = true;
        audio_queue_cv_.notify_all();
        ESP_LOGD(TAG, "Output task start: ts=%lu samples=%u playbackq=%u",
            static_cast<unsigned long>(task->timestamp),
            static_cast<unsigned>(task->pcm.size()),
            static_cast<unsigned>(audio_playback_queue_.size()));
        lock.unlock();

        if (!codec_->output_enabled()) {
            esp_timer_stop(audio_power_timer_);
            esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
            codec_->EnableOutput(true);
        }

        codec_->OutputData(task->pcm);

        auto output_finished_at = std::chrono::steady_clock::now();
        int playback_duration_ms = 0;
        auto output_rate = codec_->output_sample_rate();
        auto output_channels = codec_->output_channels();
        if (output_rate > 0 && output_channels > 0) {
            playback_duration_ms = static_cast<int>(
                task->pcm.size() * 1000 / (output_rate * output_channels));
        }
        debug_statistics_.playback_count++;
        ESP_LOGD(TAG, "Output task wrote: ts=%lu samples=%u duration_ms=%d",
            static_cast<unsigned long>(task->timestamp),
            static_cast<unsigned>(task->pcm.size()),
            playback_duration_ms);

        lock.lock();
        // i2s_channel_write can return after data is queued, before the speaker is acoustically quiet.
        auto drain_base = last_output_time_ > output_finished_at ? last_output_time_ : output_finished_at;
        last_output_time_ = drain_base + std::chrono::milliseconds(playback_duration_ms);
        playback_active_ = false;
#if CONFIG_USE_SERVER_AEC
        /* Record the timestamp for server AEC */
        if (task->timestamp > 0) {
            timestamp_queue_.push_back(task->timestamp);
        }
#endif
        audio_queue_cv_.notify_all();
        lock.unlock();
    }

    ESP_LOGW(TAG, "Audio output task stopped");
}

void AudioService::OpusCodecTask() {
    while (true) {
        std::unique_lock<std::mutex> lock(audio_queue_mutex_);
        audio_queue_cv_.wait(lock, [this]() {
            return service_stopped_ ||
                (!audio_encode_queue_.empty() && audio_send_queue_.size() < MAX_SEND_PACKETS_IN_QUEUE) ||
                (!audio_decode_queue_.empty() && audio_playback_queue_.size() < MAX_PLAYBACK_TASKS_IN_QUEUE);
        });
        if (service_stopped_) {
            break;
        }

        /* Decode the audio from decode queue */
        if (!audio_decode_queue_.empty() && audio_playback_queue_.size() < MAX_PLAYBACK_TASKS_IN_QUEUE) {
            auto packet = std::move(audio_decode_queue_.front());
            audio_decode_queue_.pop_front();
            decode_active_ = true;
            auto packet_timestamp = packet->timestamp;
            auto packet_sample_rate = packet->sample_rate;
            auto packet_frame_duration = packet->frame_duration;
            auto packet_payload_size = packet->payload.size();
            auto packet_loss_concealment = packet->loss_concealment;
            audio_queue_cv_.notify_all();
            lock.unlock();

            auto task = std::make_unique<AudioTask>();
            task->type = kAudioTaskTypeDecodeToPlaybackQueue;
            task->timestamp = packet->timestamp;

            SetDecodeSampleRate(packet->sample_rate, packet->frame_duration);
            if (opus_decoder_ != nullptr) {
                task->pcm.resize(decoder_frame_size_);
                esp_audio_dec_in_raw_t raw = {
                    .buffer = packet_loss_concealment ? nullptr : (uint8_t *)(packet->payload.data()),
                    .len = packet_loss_concealment ? 0 : (uint32_t)(packet->payload.size()),
                    .consumed = 0,
                    .frame_recover = packet_loss_concealment ? ESP_AUDIO_DEC_RECOVERY_PLC : ESP_AUDIO_DEC_RECOVERY_NONE,
                };
                esp_audio_dec_out_frame_t out_frame = {
                    .buffer = (uint8_t *)(task->pcm.data()),
                    .len = (uint32_t)(task->pcm.size() * sizeof(int16_t)),
                    .decoded_size = 0,
                };
                esp_audio_dec_info_t dec_info = {};
                std::unique_lock<std::mutex> decoder_lock(decoder_mutex_);
                auto ret = esp_opus_dec_decode(opus_decoder_, &raw, &out_frame, &dec_info);
                decoder_lock.unlock();
                if (ret == ESP_AUDIO_ERR_OK) {
                    task->pcm.resize(out_frame.decoded_size / sizeof(int16_t));
                    auto decoded_samples = task->pcm.size();
                    if (decoder_sample_rate_ != codec_->output_sample_rate() && output_resampler_ != nullptr) {
                        uint32_t target_size = 0;
                        esp_ae_rate_cvt_get_max_out_sample_num(output_resampler_, task->pcm.size(), &target_size);
                        std::vector<int16_t> resampled(target_size);
                        uint32_t actual_output = target_size;
                        esp_ae_rate_cvt_process(output_resampler_, (esp_ae_sample_t)task->pcm.data(), task->pcm.size(),
                                                (esp_ae_sample_t)resampled.data(), &actual_output);
                        resampled.resize(actual_output);
                        task->pcm = std::move(resampled);
                    }
                    auto playback_samples = task->pcm.size();
                    lock.lock();
                    ESP_LOGD(TAG, "Decode audio: ts=%lu payload=%u sr=%d frame=%d plc=%d decoded=%u playback_samples=%u playbackq=%u",
                        static_cast<unsigned long>(packet_timestamp),
                        static_cast<unsigned>(packet_payload_size),
                        packet_sample_rate,
                        packet_frame_duration,
                        packet_loss_concealment ? 1 : 0,
                        static_cast<unsigned>(decoded_samples),
                        static_cast<unsigned>(playback_samples),
                        static_cast<unsigned>(audio_playback_queue_.size()));
                    audio_playback_queue_.push_back(std::move(task));
                    audio_queue_cv_.notify_all();
                } else {
                    ESP_LOGE(TAG, "Failed to decode audio after resize, error code: %d", ret);
                    lock.lock();
                }
            } else {
                ESP_LOGE(TAG, "Audio decoder is not configured");
                lock.lock();
            }
            decode_active_ = false;
            audio_queue_cv_.notify_all();
            debug_statistics_.decode_count++;
        }
        /* Encode the audio to send queue */
        if (!audio_encode_queue_.empty() && audio_send_queue_.size() < MAX_SEND_PACKETS_IN_QUEUE) {
            auto task = std::move(audio_encode_queue_.front());
            audio_encode_queue_.pop_front();
            audio_queue_cv_.notify_all();
            lock.unlock();

            auto packet = std::make_unique<AudioStreamPacket>();
            packet->frame_duration = OPUS_FRAME_DURATION_MS;
            packet->sample_rate = 16000;
            packet->timestamp = task->timestamp;

            if (opus_encoder_ != nullptr && task->pcm.size() == encoder_frame_size_) {
                std::vector<uint8_t> buf(encoder_outbuf_size_);
                esp_audio_enc_in_frame_t in = {
                    .buffer = (uint8_t *)(task->pcm.data()),
                    .len = (uint32_t)(encoder_frame_size_ * sizeof(int16_t)),
                };
                esp_audio_enc_out_frame_t out = {
                    .buffer = buf.data(),
                    .len = (uint32_t)encoder_outbuf_size_,
                    .encoded_bytes = 0,
                };
                auto ret = esp_opus_enc_process(opus_encoder_, &in, &out);
                if (ret == ESP_AUDIO_ERR_OK) {
                    packet->payload.assign(buf.data(), buf.data() + out.encoded_bytes);

                    if (task->type == kAudioTaskTypeEncodeToSendQueue) {
                        {
                            std::lock_guard<std::mutex> lock2(audio_queue_mutex_);
                            audio_send_queue_.push_back(std::move(packet));
                        }
                        if (callbacks_.on_send_queue_available) {
                            callbacks_.on_send_queue_available();
                        }
                    } else if (task->type == kAudioTaskTypeEncodeToTestingQueue) {
                        std::lock_guard<std::mutex> lock2(audio_queue_mutex_);
                        audio_testing_queue_.push_back(std::move(packet));
                    }
                    debug_statistics_.encode_count++;
                } else {
                    ESP_LOGE(TAG, "Failed to encode audio, error code: %d", ret);
                }
            } else {
                ESP_LOGE(TAG, "Failed to encode audio: encoder not configured or invalid frame size (got %u, expected %u)",
                         task->pcm.size(), encoder_frame_size_);
            }
            lock.lock();
        }
    }

    ESP_LOGW(TAG, "Opus codec task stopped");
}

void AudioService::SetDecodeSampleRate(int sample_rate, int frame_duration) {
    if (decoder_sample_rate_ == sample_rate && decoder_duration_ms_ == frame_duration) {
        return;
    }
    std::unique_lock<std::mutex> decoder_lock(decoder_mutex_);
    if (opus_decoder_ != nullptr) {
        esp_opus_dec_close(opus_decoder_);
        opus_decoder_ = nullptr;
    }
    decoder_lock.unlock();
    esp_opus_dec_cfg_t opus_dec_cfg = OPUS_DEC_CFG(sample_rate, frame_duration);
    auto ret = esp_opus_dec_open(&opus_dec_cfg, sizeof(esp_opus_dec_cfg_t), &opus_decoder_);
    if (opus_decoder_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create audio decoder, error code: %d", ret);
        return;
    }
    decoder_sample_rate_ = sample_rate;
    decoder_duration_ms_ = frame_duration;
    decoder_frame_size_ = decoder_sample_rate_ / 1000 * frame_duration;

    auto codec = Board::GetInstance().GetAudioCodec();
    if (decoder_sample_rate_ != codec->output_sample_rate()) {
        ESP_LOGI(TAG, "Resampling audio from %d to %d", decoder_sample_rate_, codec->output_sample_rate());
        if (output_resampler_ != nullptr) {
            esp_ae_rate_cvt_close(output_resampler_);
            output_resampler_ = nullptr;
        }
        esp_ae_rate_cvt_cfg_t output_resampler_cfg = RATE_CVT_CFG(
            decoder_sample_rate_, codec->output_sample_rate(), ESP_AUDIO_MONO);
        auto resampler_ret = esp_ae_rate_cvt_open(&output_resampler_cfg, &output_resampler_);
        if (output_resampler_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create output resampler, error code: %d", resampler_ret);
        }
    }
}

void AudioService::PushTaskToEncodeQueue(AudioTaskType type, std::vector<int16_t>&& pcm, bool wait) {
    auto task = std::make_unique<AudioTask>();
    task->type = type;
    task->pcm = std::move(pcm);
    /* Push the task to the encode queue */
    std::unique_lock<std::mutex> lock(audio_queue_mutex_);

    /* If the task is to send queue, we need to set the timestamp */
    if (type == kAudioTaskTypeEncodeToSendQueue && !timestamp_queue_.empty()) {
        if (timestamp_queue_.size() <= MAX_TIMESTAMPS_IN_QUEUE) {
            task->timestamp = timestamp_queue_.front();
        } else {
            ESP_LOGW(TAG, "Timestamp queue (%u) is full, dropping timestamp", timestamp_queue_.size());
        }
        timestamp_queue_.pop_front();
    }

    if (!wait && audio_encode_queue_.size() >= MAX_ENCODE_TASKS_IN_QUEUE) {
        audio_encode_queue_.pop_front();
        debug_statistics_.encode_drop_count++;
    }
    if (wait) {
        audio_queue_cv_.wait(lock, [this]() { return audio_encode_queue_.size() < MAX_ENCODE_TASKS_IN_QUEUE; });
    }
    audio_encode_queue_.push_back(std::move(task));
    audio_queue_cv_.notify_all();
}

bool AudioService::PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket> packet, bool wait) {
    std::unique_lock<std::mutex> lock(audio_queue_mutex_);
    auto packet_timestamp = packet->timestamp;
    auto packet_sample_rate = packet->sample_rate;
    auto packet_frame_duration = packet->frame_duration;
    auto packet_payload_size = packet->payload.size();
    auto packet_sequence = packet->sequence;
    auto packet_loss_concealment = packet->loss_concealment;
    if (audio_decode_queue_.size() >= MAX_DECODE_PACKETS_IN_QUEUE) {
        if (wait) {
            ESP_LOGW(TAG, "Decode queue full, waiting: ts=%lu decodeq=%u",
                static_cast<unsigned long>(packet_timestamp),
                static_cast<unsigned>(audio_decode_queue_.size()));
            audio_queue_cv_.wait(lock, [this]() { return audio_decode_queue_.size() < MAX_DECODE_PACKETS_IN_QUEUE; });
        } else {
            ESP_LOGW(TAG, "Decode queue full, dropping: ts=%lu decodeq=%u",
                static_cast<unsigned long>(packet_timestamp),
                static_cast<unsigned>(audio_decode_queue_.size()));
            return false;
        }
    }
    decode_packet_seen_ = true;
    decode_packet_count_++;
    last_decode_packet_time_ = std::chrono::steady_clock::now();
    decode_packet_history_.push_back({
        .count = decode_packet_count_,
        .timestamp = packet_timestamp,
        .sequence = packet_sequence,
        .frame_duration = packet_frame_duration,
        .loss_concealment = packet_loss_concealment,
    });
    while (decode_packet_history_.size() > MAX_DECODE_PACKET_HISTORY) {
        decode_packet_history_.pop_front();
    }
    audio_decode_queue_.push_back(std::move(packet));
    ESP_LOGD(TAG, "Decode queue push: count=%lu ts=%lu bytes=%u sr=%d frame=%d decodeq=%u playbackq=%u active=%d",
        static_cast<unsigned long>(decode_packet_count_),
        static_cast<unsigned long>(packet_timestamp),
        static_cast<unsigned>(packet_payload_size),
        packet_sample_rate,
        packet_frame_duration,
        static_cast<unsigned>(audio_decode_queue_.size()),
        static_cast<unsigned>(audio_playback_queue_.size()),
        playback_active_ ? 1 : 0);
    audio_queue_cv_.notify_all();
    return true;
}

uint32_t AudioService::GetDecodePacketCount() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    return decode_packet_count_;
}

DecodePacketSummary AudioService::GetDecodePacketSummarySince(uint32_t start_count) {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    DecodePacketSummary summary;
    for (const auto& packet : decode_packet_history_) {
        if (packet.count <= start_count) {
            continue;
        }
        summary.packets++;
        summary.audio_ms += packet.frame_duration;
        if (packet.loss_concealment) {
            summary.plc_packets++;
        }
        if (packet.sequence > 0) {
            if (summary.first_sequence == 0) {
                summary.first_sequence = packet.sequence;
            } else if (packet.sequence > summary.last_sequence + 1) {
                summary.sequence_gaps += packet.sequence - summary.last_sequence - 1;
            }
            summary.last_sequence = packet.sequence;
        }
        if (packet.timestamp > 0) {
            if (summary.first_timestamp == 0) {
                summary.first_timestamp = packet.timestamp;
            }
            summary.last_timestamp = packet.timestamp;
        }
    }
    return summary;
}

void AudioService::ClearUploadQueues() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    audio_send_queue_.clear();
    audio_encode_queue_.erase(
        std::remove_if(audio_encode_queue_.begin(), audio_encode_queue_.end(), [](const auto& task) {
            return task && task->type == kAudioTaskTypeEncodeToSendQueue;
        }),
        audio_encode_queue_.end());
    audio_queue_cv_.notify_all();
}

std::unique_ptr<AudioStreamPacket> AudioService::PopPacketFromSendQueue() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    if (audio_send_queue_.empty()) {
        return nullptr;
    }
    auto packet = std::move(audio_send_queue_.front());
    audio_send_queue_.pop_front();
    audio_queue_cv_.notify_all();
    return packet;
}

void AudioService::NotifyPacketSent(bool success) {
    if (success) {
        debug_statistics_.send_count++;
    } else {
        debug_statistics_.send_fail_count++;
    }
}

void AudioService::EncodeWakeWord() {
    if (wake_word_) {
        wake_word_->EncodeWakeWordData();
    }
}

const std::string& AudioService::GetLastWakeWord() const {
    return wake_word_->GetLastDetectedWakeWord();
}

float AudioService::GetLastWakeRmsDbfs() const {
    if (!wake_word_) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return wake_word_->GetLastWakeRmsDbfs();
}

std::unique_ptr<AudioStreamPacket> AudioService::PopWakeWordPacket() {
    auto packet = std::make_unique<AudioStreamPacket>();
    if (wake_word_->GetWakeWordOpus(packet->payload)) {
        return packet;
    }
    return nullptr;
}

void AudioService::EnableWakeWordDetection(bool enable) {
    if (!wake_word_) {
        return;
    }

    ESP_LOGD(TAG, "%s wake word detection", enable ? "Enabling" : "Disabling");
    if (enable) {
        if (!wake_word_initialized_) {
            if (!wake_word_->Initialize(codec_, models_list_)) {
                ESP_LOGE(TAG, "Failed to initialize wake word");
                return;
            }
            wake_word_initialized_ = true;
        }
        // Reset input resampler to clear cached data from previous mode (e.g. AudioProcessor)
        // This prevents buffer overflow when switching between different feed sizes
        {
            std::lock_guard<std::mutex> lock(input_resampler_mutex_);
            if (input_resampler_ != nullptr) {
                esp_ae_rate_cvt_reset(input_resampler_);
            }
        }
        wake_word_->Start();
        xEventGroupSetBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);
    } else {
        wake_word_->Stop();
        xEventGroupClearBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);
    }
}

void AudioService::EnableVoiceProcessing(bool enable) {
    ESP_LOGD(TAG, "%s voice processing", enable ? "Enabling" : "Disabling");
    if (enable) {
        if (!audio_processor_initialized_) {
            audio_processor_->Initialize(codec_, OPUS_FRAME_DURATION_MS, models_list_);
            audio_processor_initialized_ = true;
        }

        audio_input_need_warmup_ = true;
        // Reset input resampler to clear cached data from previous mode (e.g. WakeWord)
        // This prevents buffer overflow when switching between different feed sizes
        {
            std::lock_guard<std::mutex> lock(input_resampler_mutex_);
            if (input_resampler_ != nullptr) {
                esp_ae_rate_cvt_reset(input_resampler_);
            }
        }
        audio_processor_->Start();
        xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_PROCESSOR_RUNNING);
    } else {
        audio_processor_->Stop();
        xEventGroupClearBits(event_group_, AS_EVENT_AUDIO_PROCESSOR_RUNNING);
    }
}

void AudioService::ResetVoiceProcessor() {
    if (!audio_processor_initialized_ || !(xEventGroupGetBits(event_group_) & AS_EVENT_AUDIO_PROCESSOR_RUNNING)) {
        return;
    }

    audio_processor_->Reset();
    audio_input_need_warmup_ = true;
    {
        std::lock_guard<std::mutex> lock(input_resampler_mutex_);
        if (input_resampler_ != nullptr) {
            esp_ae_rate_cvt_reset(input_resampler_);
        }
    }
}

void AudioService::EnableAudioTesting(bool enable) {
    ESP_LOGI(TAG, "%s audio testing", enable ? "Enabling" : "Disabling");
    if (enable) {
        xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING);
    } else {
        xEventGroupClearBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING);
        /* Copy audio_testing_queue_ to audio_decode_queue_ */
        std::lock_guard<std::mutex> lock(audio_queue_mutex_);
        audio_decode_queue_ = std::move(audio_testing_queue_);
        audio_queue_cv_.notify_all();
    }
}

void AudioService::EnableDeviceAec(bool enable) {
    ESP_LOGI(TAG, "%s device AEC", enable ? "Enabling" : "Disabling");
    if (!audio_processor_initialized_) {
        audio_processor_->Initialize(codec_, OPUS_FRAME_DURATION_MS, models_list_);
        audio_processor_initialized_ = true;
    }

    audio_processor_->EnableDeviceAec(enable);
}

void AudioService::SetCallbacks(AudioServiceCallbacks& callbacks) {
    callbacks_ = callbacks;
}

void AudioService::PlaySound(const std::string_view& ogg) {
    if (!codec_->output_enabled()) {
        esp_timer_stop(audio_power_timer_);
        esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
        codec_->EnableOutput(true);
    }

    const auto* buf = reinterpret_cast<const uint8_t*>(ogg.data());
    size_t size = ogg.size();

    auto demuxer = std::make_unique<OggDemuxer>();
    demuxer->OnDemuxerFinished([this](const uint8_t* data, int sample_rate, size_t size){
        auto packet = std::make_unique<AudioStreamPacket>();
        packet->sample_rate = sample_rate;
        packet->frame_duration = 60;
        packet->payload.resize(size);
        std::memcpy(packet->payload.data(), data, size);
        PushPacketToDecodeQueue(std::move(packet), true);
    });
    demuxer->Reset();
    demuxer->Process(buf, size);
}

bool AudioService::IsIdle() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    return audio_encode_queue_.empty() && audio_decode_queue_.empty() && audio_playback_queue_.empty() && audio_testing_queue_.empty();
}

bool AudioService::HasPlaybackWork() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    return !audio_decode_queue_.empty() || decode_active_ || !audio_playback_queue_.empty() || playback_active_ ||
        !IsDecodePacketIdleLocked() || IsPlaybackTailGuardActiveLocked();
}

void AudioService::WaitForPlaybackQueueEmpty(uint32_t min_decode_packet_count, int first_packet_timeout_ms) {
    std::unique_lock<std::mutex> lock(audio_queue_mutex_);
    ESP_LOGI(TAG, "Wait playback drain start: min_decode=%lu timeout=%d count=%lu decodeq=%u decode_active=%d playbackq=%u active=%d decode_idle=%d tail=%d",
        static_cast<unsigned long>(min_decode_packet_count),
        first_packet_timeout_ms,
        static_cast<unsigned long>(decode_packet_count_),
        static_cast<unsigned>(audio_decode_queue_.size()),
        decode_active_ ? 1 : 0,
        static_cast<unsigned>(audio_playback_queue_.size()),
        playback_active_ ? 1 : 0,
        IsDecodePacketIdleLocked() ? 1 : 0,
        IsPlaybackTailGuardActiveLocked() ? 1 : 0);
    if (min_decode_packet_count > 0 && first_packet_timeout_ms > 0 && decode_packet_count_ <= min_decode_packet_count) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(first_packet_timeout_ms);
        ESP_LOGI(TAG, "Wait first TTS audio packet: current=%lu min=%lu timeout=%d",
            static_cast<unsigned long>(decode_packet_count_),
            static_cast<unsigned long>(min_decode_packet_count),
            first_packet_timeout_ms);
        while (!service_stopped_ && decode_packet_count_ <= min_decode_packet_count) {
            if (audio_queue_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                break;
            }
        }
        ESP_LOGI(TAG, "Wait first TTS audio packet done: current=%lu service_stopped=%d",
            static_cast<unsigned long>(decode_packet_count_),
            service_stopped_ ? 1 : 0);
    }
    while (true) {
        audio_queue_cv_.wait(lock, [this]() {
            return service_stopped_ ||
                (audio_decode_queue_.empty() && !decode_active_ && audio_playback_queue_.empty() && !playback_active_);
        });
        if (service_stopped_) {
            ESP_LOGI(TAG, "Wait playback drain stop: service_stopped");
            playback_buffering_ = false;
            playback_buffer_min_frames_ = 0;
            playback_buffer_timeout_ms_ = 0;
            playback_buffer_started_at_ = {};
            return;
        }
        if (IsDecodePacketIdleLocked() && !IsPlaybackTailGuardActiveLocked()) {
            ESP_LOGI(TAG, "Wait playback drain done: decodeq=%u decode_active=%d playbackq=%u active=%d",
                static_cast<unsigned>(audio_decode_queue_.size()),
                decode_active_ ? 1 : 0,
                static_cast<unsigned>(audio_playback_queue_.size()),
                playback_active_ ? 1 : 0);
            playback_buffering_ = false;
            playback_buffer_min_frames_ = 0;
            playback_buffer_timeout_ms_ = 0;
            playback_buffer_started_at_ = {};
            return;
        }

        auto now = std::chrono::steady_clock::now();
        int wait_ms = 0;
        if (!IsDecodePacketIdleLocked()) {
            auto decode_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_decode_packet_time_).count();
            wait_ms = std::max(wait_ms, kDecodePacketIdleGuardMs - static_cast<int>(decode_elapsed_ms));
        }
        if (IsPlaybackTailGuardActiveLocked()) {
            auto output_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_output_time_).count();
            wait_ms = std::max(wait_ms, kPlaybackTailGuardMs - static_cast<int>(output_elapsed_ms));
        }
        wait_ms = std::max(wait_ms, 1);
        ESP_LOGD(TAG, "Wait playback drain sleep: wait_ms=%d decode_idle=%d tail=%d decodeq=%u decode_active=%d playbackq=%u active=%d",
            wait_ms,
            IsDecodePacketIdleLocked() ? 1 : 0,
            IsPlaybackTailGuardActiveLocked() ? 1 : 0,
            static_cast<unsigned>(audio_decode_queue_.size()),
            decode_active_ ? 1 : 0,
            static_cast<unsigned>(audio_playback_queue_.size()),
            playback_active_ ? 1 : 0);
        lock.unlock();
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
        lock.lock();
    }
}

bool AudioService::IsPlaybackTailGuardActiveLocked() const {
    if (codec_ == nullptr) {
        return false;
    }
    if (!codec_->input_reference() && Board::GetInstance().ShouldUploadAudioDuringSpeaking()) {
        return false;
    }

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - last_output_time_).count();
    return elapsed_ms < kPlaybackTailGuardMs;
}

bool AudioService::IsDecodePacketIdleLocked() const {
    if (!decode_packet_seen_) {
        return true;
    }

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - last_decode_packet_time_).count();
    return elapsed_ms >= kDecodePacketIdleGuardMs;
}

void AudioService::LogVoicePipelineProbe() {
    const int64_t now_us = esp_timer_get_time();
    if (now_us - last_voice_pipeline_probe_us_ < kVoicePipelineProbeIntervalUs) {
        return;
    }

    size_t send_queue_size = 0;
    size_t encode_queue_size = 0;
    {
        std::lock_guard<std::mutex> lock(audio_queue_mutex_);
        send_queue_size = audio_send_queue_.size();
        encode_queue_size = audio_encode_queue_.size();
    }

    ESP_LOGI(TAG, "voice pipeline: input=%u raw_rms=%.1f afe_feed=%u afe_out=%u out_rms=%.1f encode=%u drops=%u sent=%u send_fail=%u sendq=%u encodeq=%u",
        debug_statistics_.input_count,
        debug_statistics_.raw_input_rms,
        debug_statistics_.processor_feed_count,
        debug_statistics_.processor_output_count,
        debug_statistics_.processor_output_rms,
        debug_statistics_.encode_count,
        debug_statistics_.encode_drop_count,
        debug_statistics_.send_count,
        debug_statistics_.send_fail_count,
        static_cast<unsigned>(send_queue_size),
        static_cast<unsigned>(encode_queue_size));
    last_voice_pipeline_probe_us_ = now_us;
}

void AudioService::ResetDecoder() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    std::unique_lock<std::mutex> decoder_lock(decoder_mutex_);
    if (opus_decoder_ != nullptr) {
        esp_opus_dec_reset(opus_decoder_);
    }
    decoder_lock.unlock();
    timestamp_queue_.clear();
    audio_decode_queue_.clear();
    audio_playback_queue_.clear();
    audio_testing_queue_.clear();
    playback_buffering_ = false;
    playback_buffer_min_frames_ = 0;
    playback_buffer_timeout_ms_ = 0;
    playback_buffer_started_at_ = {};
    audio_queue_cv_.notify_all();
}

void AudioService::ResetDecoderState() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    if (!audio_decode_queue_.empty() || decode_active_ || !audio_playback_queue_.empty() || playback_active_) {
        ESP_LOGD(TAG, "Skip decoder state reset: decodeq=%u decode_active=%d playbackq=%u active=%d",
            static_cast<unsigned>(audio_decode_queue_.size()),
            decode_active_ ? 1 : 0,
            static_cast<unsigned>(audio_playback_queue_.size()),
            playback_active_ ? 1 : 0);
        return;
    }

    std::unique_lock<std::mutex> decoder_lock(decoder_mutex_);
    if (opus_decoder_ != nullptr) {
        esp_opus_dec_reset(opus_decoder_);
    }
    if (output_resampler_ != nullptr) {
        auto ret = esp_ae_rate_cvt_reset(output_resampler_);
        if (ret != ESP_AE_ERR_OK) {
            ESP_LOGW(TAG, "Failed to reset output resampler, error code: %d", ret);
        }
    }
    ESP_LOGI(TAG, "Decoder state reset");
}

void AudioService::BeginPlaybackBuffering(size_t min_frames, int timeout_ms) {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    playback_buffering_ = min_frames > 1 && timeout_ms > 0;
    playback_buffer_min_frames_ = playback_buffering_ ? min_frames : 0;
    playback_buffer_timeout_ms_ = playback_buffering_ ? timeout_ms : 0;
    playback_buffer_started_at_ = {};
    if (playback_buffering_) {
        ESP_LOGI(TAG, "Playback buffer begin: min=%u timeout=%d",
            static_cast<unsigned>(playback_buffer_min_frames_),
            playback_buffer_timeout_ms_);
    }
    audio_queue_cv_.notify_all();
}

void AudioService::CheckAndUpdateAudioPowerState() {
    auto now = std::chrono::steady_clock::now();
    auto input_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_input_time_).count();
    auto output_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_output_time_).count();
    if (input_elapsed > AUDIO_POWER_TIMEOUT_MS && codec_->input_enabled()) {
        codec_->EnableInput(false);
    }
    if (output_elapsed > AUDIO_POWER_TIMEOUT_MS && codec_->output_enabled()) {
        // Keep TX clock when duplex RX is active; otherwise RX may stall on some boards.
        if (!(codec_->duplex() && codec_->input_enabled())) {
            codec_->EnableOutput(false);
        }
    }
    if (!codec_->input_enabled() && !codec_->output_enabled()) {
        esp_timer_stop(audio_power_timer_);
    }
}

void AudioService::SetModelsList(srmodel_list_t* models_list) {
    models_list_ = models_list;

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
    if (esp_srmodel_filter(models_list_, ESP_MN_PREFIX, NULL) != nullptr) {
        wake_word_ = std::make_unique<CustomWakeWord>();
    } else if (esp_srmodel_filter(models_list_, ESP_WN_PREFIX, NULL) != nullptr) {
        wake_word_ = std::make_unique<AfeWakeWord>();
    } else {
        wake_word_ = nullptr;
    }
#else
    if (esp_srmodel_filter(models_list_, ESP_WN_PREFIX, NULL) != nullptr) {
        wake_word_ = std::make_unique<EspWakeWord>();
    } else {
        wake_word_ = nullptr;
    }
#endif

    if (wake_word_) {
        wake_word_->OnWakeWordDetected([this](const std::string& wake_word) {
            if (callbacks_.on_wake_word_detected) {
                callbacks_.on_wake_word_detected(wake_word);
            }
        });
    }
}

bool AudioService::IsAfeWakeWord() {
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
    return wake_word_ != nullptr && dynamic_cast<AfeWakeWord*>(wake_word_.get()) != nullptr;
#else
    return false;
#endif
}
