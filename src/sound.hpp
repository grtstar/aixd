#pragma once
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <portaudio.h>
#include <algorithm>

typedef int (*audioCallback)(const void* inputBuffer, void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData);

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
    SoundDev() : sample_rate(44100), sample_format(0), frames_per_buffer(1024), channels(2) {}
    virtual ~SoundDev() {}
    int AudioCallback(const void* inputBuffer, void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData)
    {
        for (const auto& cb : cbs) {
            cb.cb(inputBuffer, outputBuffer, framesPerBuffer, timeInfo, statusFlags, cb.data);
        }
        return 0;
    }
    void AddCb(audioCallback cb, void *data)
    {
        cbs.push_back({cb, data});
    }
    void RemoveCb(audioCallback cb)
    {
        cbs.erase(
            std::remove_if(cbs.begin(), cbs.end(),
                [cb](const SoundCb& scb) { return scb.cb == cb; }),
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

class PlayDev: public SoundDev
{
public:
    virtual int Open() override
    {
        // Open the playback device
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            printf("无法初始化 PortAudio: %s\n", Pa_GetErrorText(err));
            return -1;
        }
        PaStreamParameters outputParams;
        outputParams.device = Pa_GetDefaultOutputDevice();
        outputParams.channelCount = channels;
        outputParams.sampleFormat = sample_format; // paInt16, paFloat32, etc.
        outputParams.suggestedLatency = Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
        outputParams.hostApiSpecificStreamInfo = NULL;
        err = Pa_OpenStream(&stream, NULL, &outputParams, sample_rate, frames_per_buffer, paClipOff, 
                            [&](const void* inputBuffer, void* outputBuffer,
                               unsigned long framesPerBuffer,
                               const PaStreamCallbackTimeInfo* timeInfo,
                               PaStreamCallbackFlags statusFlags,
                               void* userData) {
                                return AudioCallback(inputBuffer, outputBuffer, framesPerBuffer, timeInfo, statusFlags, userData);
                            }, this);
        if (err != paNoError) {
            printf("无法打开音频流: %s\n", Pa_GetErrorText(err));
            return -1;
        }
        return 0;
    }
    virtual int Close() override
    {
        // Close the playback device
        Pa_StopStream(stream);
        return Pa_CloseStream(stream);
    }
};

class RecordDev : public SoundDev
{
public:    
    virtual int Open() override
    {
        // Open the recording device
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            printf("无法初始化 PortAudio: %s\n", Pa_GetErrorText(err));
            return -1;
        }
        PaStreamParameters inputParams;
        inputParams.device = Pa_GetDefaultInputDevice();
        inputParams.channelCount = channels;
        inputParams.sampleFormat = sample_format; // paInt16, paFloat32, etc.
        inputParams.suggestedLatency = Pa_GetDeviceInfo(inputParams.device)->defaultLowInputLatency;
        inputParams.hostApiSpecificStreamInfo = NULL;
        err = Pa_OpenStream(&stream, &inputParams, NULL, sample_rate, frames_per_buffer, paClipOff, 
                            [&](const void* inputBuffer, void* outputBuffer,
                               unsigned long framesPerBuffer,
                               const PaStreamCallbackTimeInfo* timeInfo,
                               PaStreamCallbackFlags statusFlags,
                               void* userData) {
                                return AudioCallback(inputBuffer, outputBuffer, framesPerBuffer, timeInfo, statusFlags, userData);
                            }, this);
        if (err != paNoError) {
            printf("无法打开音频流: %s\n", Pa_GetErrorText(err));
            return -1;
        }
        err = Pa_StartStream(stream);
        if (err != paNoError) {
            printf("无法启动音频流: %s\n", Pa_GetErrorText(err));
            return -1;
        }
        return 0;
    }
    virtual int Close() override
    {
        // Close the recording device
        Pa_StopStream(stream);
        return Pa_CloseStream(stream);
    }
};

class AudioPool
{
private:
    std::mutex mutex_;
    std::vector<std::vector<uint8_t>> audio_pool_;
    int max_cnt;
public:
    AudioPool(int cnt = 100) {max_cnt = cnt;}
    ~AudioPool() {}

    void add_audio(const std::vector<uint8_t> &audio)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        audio_pool_.push_back(audio);
    }

    std::vector<uint8_t> get_audio()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (audio_pool_.empty())
            return {};
        auto audio = audio_pool_.front();
        audio_pool_.erase(audio_pool_.begin());
        return audio;
    }
};