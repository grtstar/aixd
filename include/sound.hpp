#pragma once
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <chrono> 

#include <portaudio.h>
#include <algorithm>
#include <portaudio.h>
#include <miniaudio.h>

using audioCallback = void (*)(void *pUserData, 
                    void *pOutput, 
                    const void *pInput, 
                    ma_uint32 frameCount);

struct SoundCb
{
    audioCallback cb;
    void *data;
};

class SoundDev
{
protected:
    std::vector<SoundCb> cbs;
    PaStream *stream;
public:
    int sample_rate;
    uint32_t sample_format;
    int frames_per_buffer;
    int channels;

public:
    SoundDev(int sample_rate, uint32_t sample_format, int frames_per_buffer, int channels)
        : sample_rate(sample_rate), sample_format(sample_format), frames_per_buffer(frames_per_buffer), channels(channels) {}
    virtual ~SoundDev() {}
    static void AudioCallback(ma_device *pDevice, 
                    void *pOutput, 
                    const void *pInput, 
                    ma_uint32 frameCount)
    {
        SoundDev *pSoundDev = static_cast<SoundDev *>(pDevice->pUserData);
        for (const auto &cb : pSoundDev->cbs)
        {
            cb.cb(cb.data, pOutput, pInput, frameCount);
        }
    }
    void AddCb(audioCallback cb, void *data)
    {
        cbs.push_back({cb, data});
    }
    void RemoveCb(audioCallback cb)
    {
        cbs.erase(
            std::remove_if(cbs.begin(), cbs.end(),
                           [cb](const SoundCb &scb)
                           { return scb.cb == cb; }),
            cbs.end());
    }
    virtual int Open()
    {
        // Open the audio device
        return 0;
    }
    virtual int Close()
    {
        // Close the audio device
        return 0;
    }
    int BytesPerSample() const
    {
        return ma_get_bytes_per_sample((ma_format)sample_format);
    }
    int BytesPerFrame() const
    {
        return ma_get_bytes_per_frame((ma_format)sample_format, channels);
    }
};


class AudioQueue
{
private:
    std::mutex mutex_;
    std::deque<uint8_t> audio_queue_;
    int max_size = 100 * 1024; // Default max size of 100 KB

public:

    AudioQueue() {}
    ~AudioQueue() {}

    void push(const std::vector<uint8_t> &audio)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        audio_queue_.insert(audio_queue_.end(), audio.begin(), audio.end());
    }

    std::vector<uint8_t> pop_front(size_t size = 1024)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (audio_queue_.size() < size)
        {
            size = audio_queue_.size();
        }
        std::vector<uint8_t> audio(audio_queue_.begin(), audio_queue_.begin() + size);
        audio_queue_.erase(audio_queue_.begin(), audio_queue_.begin() + size);
        return audio;
    }
};

class PcmConverter
{
private:
    ma_data_converter converter;
    ma_data_converter_config config;
    int inFormat;
    int inSampleRate;
    int inChannels;
    int outFormat;
    int outSampleRate;
    int outChannels;
public:
    PcmConverter(int inFormat, int inSampleRate, int inChannels,
                   int outFormat, int outSampleRate, int outChannels)
        : inFormat(inFormat), inSampleRate(inSampleRate), inChannels(inChannels),
          outFormat(outFormat), outSampleRate(outSampleRate), outChannels(outChannels)
    {
        config = ma_data_converter_config_init(
            (ma_format)inFormat, (ma_format)outFormat, inChannels, outChannels,
            inSampleRate, outSampleRate);
        if (ma_data_converter_init(&config, NULL, &converter) != MA_SUCCESS) {
            throw std::runtime_error("Failed to initialize PCM converter");
        }
    }
    ~PcmConverter()
    {
        ma_data_converter_uninit(&converter, NULL);
    }
    static int GetBytesPerFrame(int format, int channels)
    {
        return ma_get_bytes_per_frame((ma_format)format, channels);
    }
    std::vector<uint8_t> Convert(const void*data, size_t size)
    {
        if (data == nullptr || size == 0) {
            return {};
        }
        size_t inFrameSize = GetBytesPerFrame(inFormat, inChannels);
        size_t outFrameSize = GetBytesPerFrame(outFormat, outChannels);
        ma_uint64 inFrameCount = size / inFrameSize;
        ma_uint64 outFrameCount = inFrameCount * outSampleRate / inSampleRate + 1;
        std::vector<uint8_t> outputData(outFrameCount * outFrameSize);
        ma_uint64 framesConverted = outFrameCount;
        
        ma_result result = ma_data_converter_process_pcm_frames(
            &converter,
            data, &inFrameCount,
            outputData.data(), &framesConverted);

        if (result != MA_SUCCESS) {
            printf("PCM conversion failed\n");
            outputData.clear();
        } else {
            outputData.resize(framesConverted * outFrameSize);
        }
        return outputData;
    }
    std::vector<uint8_t> Convert(const std::vector<uint8_t> &inputData)
    {
        return Convert(inputData.data(), inputData.size());
    }   
};

class PlayDev : public SoundDev
{
    ma_device device;
    ma_context context;
    AudioQueue audio_queue_;
public:
    using SoundDev::SoundDev;
    virtual int Open() override
    {
        ma_backend backends[] = {ma_backend_alsa};
        ma_context_config ctxConfig = ma_context_config_init();

        if (ma_context_init(backends, 1, &ctxConfig, &context) != MA_SUCCESS)
        {
            printf("Failed to initialize miniaudio context\n");
            return -1;
        }

        ma_device_config devConfig = ma_device_config_init(ma_device_type_playback);
        devConfig.playback.format = (ma_format)sample_format; // or ma_format_f32
        devConfig.playback.channels = channels;
        devConfig.sampleRate = sample_rate;
        devConfig.dataCallback = AudioCallback; // Set your callback function
        devConfig.pUserData = this; // Pass pointer to your audio queue if needed

        if (ma_device_init(&context, &devConfig, &device) != MA_SUCCESS)
        {
            printf("Failed to open playback device\n");
            ma_context_uninit(&context);
            return -1;
        }
        if (ma_device_start(&device) != MA_SUCCESS)
        {
            printf("Failed to start playback device\n");
            ma_device_uninit(&device);
            ma_context_uninit(&context);
            return -1;
        }

        AddCb(PlayCb, this); // Register the playback callback
        return 0;
    }
    virtual int Close() override
    {
        RemoveCb(PlayCb); // Unregister the playback callback
        ma_device_uninit(&device);
        ma_context_uninit(&context);
        return 0;
    }
    static void PlayCb(void *pUserData, 
                    void *pOutput, 
                    const void *pInput, 
                    ma_uint32 frameCount)
    {
        PlayDev *pPlayDev = static_cast<PlayDev *>(pUserData);
        std::vector<uint8_t> audioData = pPlayDev->audio_queue_.pop_front(frameCount * pPlayDev->BytesPerFrame());
        if (!audioData.empty())
        {
            memcpy(pOutput, audioData.data(), audioData.size());
        }
        else
        {
            // Fill with silence if no data available
            // std::fill((uint8_t *)pOutput, (uint8_t *)pOutput + frameCount * pPlayDev->BytesPerFrame(), 0);
        }
    }
    void Play(void *data, size_t size)
    {
        if (data == nullptr || size == 0)
        {
            return;
        }
        audio_queue_.push(std::vector<uint8_t>((uint8_t *)data, (uint8_t *)data + size));
    }
    void Play(void *data, size_t size, int sample_rate, uint32_t sample_format, int channels)
    {
        if (data == nullptr || size == 0)
        {
            return;
        }
        // Adjust the playback parameters if needed
        PcmConverter converter(
            (int)sample_format, sample_rate, channels,
            (int)this->sample_format, this->sample_rate, this->channels);
        std::vector<uint8_t> convertedData = converter.Convert(data, size);
        if (convertedData.empty())
        {
            return;
        }
        audio_queue_.push(convertedData);
    }
};

class RecordDev : public SoundDev
{
    ma_device device;
    ma_context context;
public:
    using SoundDev::SoundDev;
    virtual int Open() override
    {
        // Open the recording device
        ma_backend backends[] = {ma_backend_alsa};
        ma_context_config ctxConfig = ma_context_config_init();
        if (ma_context_init(backends, 1, &ctxConfig, &context) != MA_SUCCESS)
        {
            printf("Failed to initialize miniaudio context\n");
            return -1;
        }
        ma_device_config devConfig = ma_device_config_init(ma_device_type_capture);
        devConfig.capture.format = (ma_format)sample_format; // e.g., ma_format_f32
        devConfig.capture.channels = channels;
        devConfig.sampleRate = sample_rate;
        devConfig.dataCallback = AudioCallback; // Set your callback if needed
        devConfig.pUserData = this; // Pass pointer to your audio queue if needed
        if (ma_device_init(&context, &devConfig, &device) != MA_SUCCESS)
        {
            printf("Failed to open recording device\n");
            ma_context_uninit(&context);
            return -1;
        }
        if (ma_device_start(&device) != MA_SUCCESS)
        {
            printf("Failed to start recording device\n");
            ma_device_uninit(&device);
            ma_context_uninit(&context);
            return -1;
        }
        return 0;
    }
    virtual int Close() override
    {
        // Close the recording device
        ma_device_uninit(&device);
        ma_context_uninit(&context);
        return 0;
    }
};
