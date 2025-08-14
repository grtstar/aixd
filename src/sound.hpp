#pragma once
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <deque>
#include <portaudio.h>
#include <algorithm>
#include <portaudio.h>
#include <miniaudio.h>

typedef void (*audioCallback)(void *pUserData, 
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
    int sample_rate;
    uint32_t sample_format;
    int frames_per_buffer;
    int channels;
    PaStream *stream;

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
};

class PlayDev : public SoundDev
{
    ma_device device;
    ma_context context;

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
        return 0;
    }
    virtual int Close() override
    {
        ma_device_uninit(&device);
        ma_context_uninit(&context);
        return 0;
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

class AudioTool
{
public:
    static int CountBytesPerSample(ma_format sample_format, int channels)
    {
        switch (sample_format)
        {
        case ma_format_u8:
            return 1 * channels; // 8-bit unsigned
        case ma_format_s16:
            return 2 * channels; // 16-bit signed
        case ma_format_s24:
            return 3 * channels; // 24-bit signed, tightly packed
        case ma_format_s32:
            return 4 * channels; // 32-bit signed
        case ma_format_f32:
            return 4 * channels; // 32-bit float
        default:
            return 0; // Unsupported format
        }
    }
    static std::vector<uint8_t> ConvertPcm(int inFormat, int inSampleRate, int inChannels,
                                           int outFormat, int outSampleRate, int outChannels,
                                           const std::vector<uint8_t> &inputData)
    {
        std::vector<uint8_t> outputData;
        ma_data_converter converter;
        ma_data_converter_config config = ma_data_converter_config_init(
            (ma_format)inFormat, (ma_format)outFormat, inChannels, outChannels,
            inSampleRate, outSampleRate);

        if (ma_data_converter_init(&config, NULL, &converter) != MA_SUCCESS)
        {
            printf("Failed to initialize PCM converter\n");
            return outputData;
        }

        size_t inFrameSize = ma_get_bytes_per_frame((ma_format)inFormat, inChannels);
        size_t outFrameSize = ma_get_bytes_per_frame((ma_format)outFormat, outChannels);
        ma_uint64 inFrameCount = inputData.size() / inFrameSize;
        ma_uint64 outFrameCount = inFrameCount * outSampleRate / inSampleRate + 1;

        outputData.resize(outFrameCount * outFrameSize);

        ma_uint64 framesConverted = outFrameCount;
        ma_result result = ma_data_converter_process_pcm_frames(
            &converter,
            inputData.data(), &inFrameCount,
            outputData.data(), &framesConverted);

        if (result != MA_SUCCESS)
        {
            printf("PCM conversion failed\n");
            outputData.clear();
        }
        else
        {
            outputData.resize(framesConverted * outFrameSize);
        }

        ma_data_converter_uninit(&converter, NULL);
        return outputData;
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

    std::vector<uint8_t> pop_front(int size = 1024)
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