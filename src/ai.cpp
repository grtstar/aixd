#include <stdint.h>
#include <mutex>
#include <thread>
#include <chrono> 
#include <regex> 
#include <fstream>
#include <lcm/lcm-cpp.hpp>
#include <lcm/lcm.h>
#include "log_.h"
#include <mars_message/String.hpp>
// #include "aixd.h"

#define TAG "AIXD"
#if 0

#define SIMPLEWEB_USE_STANDALONE_ASIO 1
#define ASIO_USE_TS_EXECUTOR_AS_DEFAULT  1
#include "simpleweb/wss_client.hpp"
#include "json.hpp"

#include "rksound.h"

using SimpleWeb::WssClient;
using namespace std;


enum MessageType
{
    FULL_CLIENT_REQ = 0x1,  // 客户端发送文本事件
    FULL_SERVER_RSP = 0x9,  // 服务器返回文本事件
    AUDIO_ONLY_REQ = 0x2,   // 客户端发送音频数据
    AUDIO_ONLY_RSP = 0xB,   // 服务器返回音频数据
    ERROR_INFO = 0xF        // 服务器返回错误事件
};

enum MessageFlags
{
    NO_SEQUENCE = 0,        // 没有 sequence 字段
    POS_SEQUENCE = 1,       // 序号大于 0 的非终端数据包
    NEG_SEQUENCE = 2,       // 最后一个无序号的数据包
    NEG_SEQUENCE_1 = 3,     // 最后一个序号小于 0 的数据包
    MSG_WITH_EVENT = 4,     // 携带事件 ID
};

enum MessageSerialization
{
    NO_SERIALIZATION = 0,
    JSON = 1,
    THRIFT = 3,
    CUSTOM_SERIALIZATION = 0xF
};

enum MessageCompression
{
    NO_COMPRESSION = 0,
    GZIP = 3,
    CUSTOM_COMPRESSION = 0xF
};

enum Event
{
    StartConnect = 1,
    FinishConnection = 2,
    StartSession = 100,
    FinishSession = 102,
    TaskRequest = 200,
    SayHello = 300,
    ChatTTSText = 500,
    ConnectionStarted = 50,
    ConnectionFailed = 51,
    ConnectionFinished = 52,
    SessionStarted = 150,
    SessionFinished = 152,
    SessionFailed = 153,
    TTSSentenceStart = 350,
    TTSSentenceEnd = 351,
    TTSResponse = 352,
    TTSEnded = 359,
    ASRInfo = 450,
    ASRResponse = 451,
    ASREnded = 459,
    ChatResponse = 550,
    ChatEnded = 559,
};

std::string huoshan_session = R"(
{
    "tts": {
        "audio_config": {
            "channel": 1,
            "format": "pcm",
            "sample_rate": 24000
        }
    },
    "dialog": {
        "bot_name": "小缘",
        "system_role": "你是一个超有智能的管家机器人，你还会照顾宠物，帮小学生写作业，你使用活泼灵动的女声，性格开朗，热爱生活。我叫了你的名字后你才开始回应我",
        "speaking_style": "你的说话风格简洁明了，语速适中，语调自然。",
        "extra": {
            "strict_audit": false
        }
    }
})";
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

AudioPool apool;
AudioPool micpool(20);

struct HuoshanProto
{
    struct Header
    {
        uint8_t header_size:4;
        uint8_t version:4;
        uint8_t message_flags:4;
        uint8_t message_type:4;
        uint8_t compression:4;
        uint8_t serialization:4;
        uint8_t reserved;
    }header;
    struct Optional
    {
        uint32_t code;
        uint32_t seq;
        uint32_t event;
        uint32_t conn_id_size;
        std::string conn_id;
        uint32_t sess_id_size;
        std::string sess_id;
    }optional;
    uint32_t payload_size;
    std::string payload;
    uint32_t session_id_size;
    std::string session_id;
    bool is_ready = false;
    int play_idle = 0;
    bool disabled_remote = false;
    std::string prompt;
    std::string hello;
    HuoshanProto()
    {

    }
    HuoshanProto(std::string session_id, std::string prompt, std::string hello)
    {
        this->session_id = session_id;
        this->prompt = prompt;
        this->hello = hello;
    }

    std::string genrate_header(uint8_t version = 0x1, 
                                        uint8_t message_type = FULL_CLIENT_REQ,
                                        uint8_t message_flags = MSG_WITH_EVENT,
                                        uint8_t serialization = JSON,
                                        uint8_t compression = NO_COMPRESSION)
    {
        Header header;
        header.version = version;
        header.header_size = 1;
        header.message_type = message_type;
        header.message_flags = message_flags;
        header.serialization = serialization;
        header.compression = compression;
        std::string buffer;
        uint8_t *d = (uint8_t *)&header;
        for(size_t i=0; i<sizeof(Header); i++)
        {
            buffer.push_back(d[i]);
        }
        return buffer;
    }

    std::string to_bytesb(uint32_t d)
    {
        // d = htonl(d); // Convert to network byte order (big-endian)
        // Create a vector to hold the bytes in big-endian order
        std::string  buffer(sizeof(d), '\0');
        // Convert d to big-endian representation
        buffer[0] = (d >> 24) & 0xFF;
        buffer[1] = (d >> 16) & 0xFF;
        buffer[2] = (d >> 8) & 0xFF;
        buffer[3] = d & 0xFF;
        return buffer;
    }
    int from_byteb(char* bytes)
    {
        int r = (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
        return r;
    }

    HuoshanProto parse(std::string response)
    {
        HuoshanProto hp;
        hp.header.version = response[0] >> 4;
        hp.header.header_size = response[0] & 0x0F;
        hp.header.message_type = response[1] >> 4;
        hp.header.message_flags = response[1] & 0x0F;
        hp.header.serialization = response[2] >> 4;
        hp.header.compression = response[2] & 0x0F;
        LOGD(TAG, "HS: len:{}, message_type: {}, serialization: {}, compression: {}", response.length(), (int)hp.header.message_type, (int)hp.header.serialization, (int)hp.header.compression);
        hp.header.reserved = response[3];
        auto op = &response[4];
        int start = 0;
        if(hp.header.message_type == FULL_SERVER_RSP || hp.header.message_type == AUDIO_ONLY_RSP)
        {
            if(hp.header.message_flags & NEG_SEQUENCE)
            {
                hp.optional.seq = from_byteb(&op[start]);
                start += 4;
            }
            if(hp.header.message_flags & MSG_WITH_EVENT)
            {
                hp.optional.event = from_byteb(&op[start]);
                if(hp.optional.event == Event::SessionStarted)
                {
                    is_ready = true;
                }
                else if(hp.optional.event == Event::SessionFinished)
                {
                    is_ready = false;
                }
                LOGD(TAG, "HS: event: {}", hp.optional.event);
                start += 4;
            }
            hp.session_id_size = from_byteb(&op[start]);
            start += sizeof(hp.session_id_size);
            hp.session_id.insert(hp.session_id.end(), &op[start], &op[start] + hp.session_id_size);
            start += hp.session_id_size;
            hp.payload_size = from_byteb(&op[start]);
            start += sizeof(hp.payload_size);
            hp.payload.insert(hp.payload.end(), &op[start], &op[start] + hp.payload_size);
            if(hp.header.serialization == JSON)
            {
                hp.payload += "\0";
                LOGD(TAG, "HS: payload: {}", hp.payload.c_str());
            }
            if(hp.optional.event == TTSSentenceStart)
            {
                if(disabled_remote)
                {
                    nlohmann::json json = nlohmann::json::parse(hp.payload);
                    if(json["tts_type"] == "chat_tts_text")
                    {
                        disabled_remote = false;
                        LOGD(TAG, "HS: enable remote");
                    }
                }
            }
            if(hp.header.message_type == AUDIO_ONLY_RSP)
            {
                // printf("HS: audio: %d\n", hp.payload_size);
                // Convert 24000Hz Float32 audio to 8000Hz by taking every third sample
                
                if(!disabled_remote)
                {
                    std::vector<uint8_t> audio(hp.payload.size() / 4 / 3 * 4); // 24000Hz Float32 audio to 8000Hz Float32 audio
                    for(size_t i=0; i<audio.size(); i+=4)
                    {
                        audio[i+0] = op[start + i * 3 + 0];
                        audio[i+1] = op[start + i * 3 + 1];
                        audio[i+2] = op[start + i * 3 + 2];
                        audio[i+3] = op[start + i * 3 + 3];
                    }
                    apool.add_audio(audio);
                }
            }
        }
        else if(hp.header.message_type == ERROR_INFO)
        {
            hp.optional.code = from_byteb(&op[start]);
            LOGD(TAG, "HS: error: {}", hp.optional.code);
            start += 4;
            hp.payload_size = from_byteb(&op[start]);
            start += sizeof(hp.payload_size);
            LOGD(TAG, "HS: payload_size: {}", hp.payload_size);
            hp.payload.insert(hp.payload.end(), &op[start], &op[start] + hp.payload_size);
            if(hp.header.serialization == JSON)
            {
                hp.payload += "\0";
                LOGD(TAG, "HS: payload: {}", hp.payload.c_str());
            }
        }
        return hp;
    }

    std::string StartConnect()
    {
        LOGD(TAG, "Huoshan: StartConnect");
        auto d = genrate_header();
        d.append(to_bytesb(Event::StartConnect));
        std::string json = "{}";
        d.append(to_bytesb(json.length())); // payload size
        d.append(json); // payload
        return d;
    }
    std::string FinishConnect()
    {
        LOGD(TAG, "HS: FinishConnect");
        auto d = genrate_header();
        d.append(to_bytesb(Event::FinishConnection));
        std::string json = "{}";
        d.append(to_bytesb(json.size())); // payload size
        d.append(json); // payload
        return d;
    }
    
    std::string StartSession()
    {
        LOGD(TAG, "HS: StartSession");
        auto d = genrate_header();
        d += to_bytesb(Event::StartSession);
       
        d += to_bytesb(session_id.length());
        d += session_id;
        
        d += to_bytesb(prompt.length());
        d += prompt;
        return d;
    }
    std::string TaskRequest(uint8_t* audio, int len)
    {
        //printf("HS: TaskRequest\n");
        auto d = genrate_header(0x1, AUDIO_ONLY_REQ, MSG_WITH_EVENT, NO_SERIALIZATION);
        d += to_bytesb(Event::TaskRequest);
        
        d += to_bytesb(session_id.length());
        d += session_id;

        auto payload_size = to_bytesb(len);
        d += to_bytesb(len); // payload size
        d.insert(d.end(), audio, audio + len); // payload
        return d;
    }
    std::string FinishSession()
    {
        LOGD(TAG, "HS: FinishSession");
        auto d = genrate_header();
        auto opt = to_bytesb(Event::FinishSession);
        d.insert(d.end(), opt.begin(), opt.end());

        auto sid_size = to_bytesb(session_id.length());
        d.insert(d.end(), sid_size.begin(), sid_size.end());
        d.insert(d.end(), session_id.begin(), session_id.end());

        std::string json = "{}";
        auto size_bytes = to_bytesb(json.size());
        d.insert(d.end(), size_bytes.begin(), size_bytes.end()); // payload size
        d.insert(d.end(), json.begin(), json.end()); // payload
        return d;
    }
    std::string SayHello()
    {
        LOGD(TAG, "HS: SayHello");
        auto d = genrate_header();
        auto opt = to_bytesb(Event::SayHello);
        d.insert(d.end(), opt.begin(), opt.end());

        auto sid_size = to_bytesb(session_id.length());
        d.insert(d.end(), sid_size.begin(), sid_size.end());
        d.insert(d.end(), session_id.begin(), session_id.end());

        nlohmann::json json;
        json["content"] = hello;
        std::string json_str = json.dump();
        auto size_bytes = to_bytesb(json_str.size());
        d.insert(d.end(), size_bytes.begin(), size_bytes.end()); // payload size
        d.insert(d.end(), json_str.begin(), json_str.end()); // payload
        return d;
    }
    std::string SayHello(const std::string &content)
    {
        LOGD(TAG, "HS: SayHello");
        auto d = genrate_header();
        auto opt = to_bytesb(Event::SayHello);
        d.insert(d.end(), opt.begin(), opt.end());

        auto sid_size = to_bytesb(session_id.length());
        d.insert(d.end(), sid_size.begin(), sid_size.end());
        d.insert(d.end(), session_id.begin(), session_id.end());

        nlohmann::json json;
        json["content"] = content;
        std::string json_str = json.dump();
        auto size_bytes = to_bytesb(json_str.size());
        d.insert(d.end(), size_bytes.begin(), size_bytes.end()); // payload size
        d.insert(d.end(), json_str.begin(), json_str.end()); // payload
        return d;
    }
    std::string ChatTTSText(const std::string &text, bool start, bool end)
    {
        LOGD(TAG, "HS: ChatTTSText: {}", text.c_str());
        auto d = genrate_header();
        auto opt = to_bytesb(Event::ChatTTSText);
        d.insert(d.end(), opt.begin(), opt.end());

        auto sid_size = to_bytesb(session_id.length());
        d.insert(d.end(), sid_size.begin(), sid_size.end());
        d.insert(d.end(), session_id.begin(), session_id.end());

        nlohmann::json json;
        json["start"] = start;
        json["content"] = text;
        json["end"] = end;
        std::string json_str = json.dump();
        auto size_bytes = to_bytesb(json_str.size());
        d.insert(d.end(), size_bytes.begin(), size_bytes.end()); // payload size
        d.insert(d.end(), json_str.begin(), json_str.end()); // payload
        return d;
    }
};

class LocalCmd
{
public:
    std::string function;
    std::string params;
public:
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LocalCmd, function, params)
};
class LocalAction
{
public:
    std::string name;
    std::vector<std::string> patterns;
    LocalCmd cmd;
    std::vector<std::string> replysp;
    std::vector<std::string> replysn;
public:
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LocalAction, name, patterns, cmd, replysp, replysn)
public:
    bool Match(std::string tts)
    {
        for(auto & pattern : patterns)
        {
            std::regex re(pattern);
            if(std::regex_match(tts, re))
            {
                LOGD("TAG", "Match {} with {}", tts.c_str(), pattern.c_str());
                return true;
            }
        }
        return false;
    }
};


class AiConfigs
{
    nlohmann::json j;
public:
    AiConfigs(const std::string &path)
    {
        std::ifstream ifs(path);
        if (ifs.good())
        {
            try
            {
                j = nlohmann::json::parse(ifs);
            }
            catch (const std::exception &e)
            {
                LOGE(TAG, "RCFG: {}", e.what());
            }
        }
    }
    nlohmann::json & GetJson()
    {
        return j;
    }
};
class LocalAi
{
    std::vector<LocalAction> actions;
public:
    bool LoadAction(nlohmann::json & j)
    {
        try
        {
            for(auto & action : j["actions"])
            {
                LocalAction a;
                a.name = action["name"];
                a.patterns = action["patterns"].get<std::vector<std::string>>();
                a.cmd.function = action["cmd"]["function"];
                a.cmd.params = action["cmd"]["param"];
                if(action.contains("replysp"))
                    a.replysp = action["replysp"].get<std::vector<std::string>>();
                if(action.contains("replysn"))
                    a.replysn = action["replysn"].get<std::vector<std::string>>();
                actions.push_back(a);
            }
        }
        catch (const std::exception &e)
        {
            LOGE("RCFG", "{}", e.what());
            return false;
        }
        return true;
    }
    LocalAction MatchAction(std::string tts)
    {
        for(auto & action : actions)
        {
            if(action.Match(tts))
            {
                return action;
            }
        }
        return LocalAction();
    }
};

LocalAi local_ai;
std::string last_asr;

/**
 * @brief  音频录制回调
 * @param  [IN CONST void*] 输入缓冲区
 * @param  [INOUT void*] 输出缓冲区
 * @param  [IN unsigned long] 每帧的大小
 * @param  [IN CONST PaStreamCallbackTimeInfo*] 时间信息
 * @param  [IN PaStreamCallbackFlags] 状态标志
 * @param  [IN void*] 用户数据
 * @return [*]
 */
int record_callback(const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData)
{   
        // 取第四个通道的数据
        uint16_t * channel_data = (uint16_t*)malloc(framesPerBuffer * sizeof(uint16_t));
        if (channel_data == NULL) {
            LOGE(TAG, "内存分配失败");
            return 1;
        }
        const int channel_index = 3;
        for(size_t i=0; i<framesPerBuffer; i++)
        {
            channel_data[i] = ((uint16_t*)inputBuffer)[i * 4 + channel_index];
        }
        // 将音频数据写入 ring buffer
        micpool.add_audio(std::vector<uint8_t>((uint8_t*)channel_data, (uint8_t*)channel_data + framesPerBuffer * sizeof(uint16_t)));
        free(channel_data);
    return 0;
}

std::string RandReply(std::vector<std::string> replys)
{
    if(replys.empty())
        return "";
    int idx = rand() % replys.size();
    return replys[idx];
}

std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

int main(int argc, char *argv[])
{
    lcm::LCM lcm;

    if (!lcm.good())
    {
        LOGE(TAG, "LCM no good");
        return 1;
    }

    rksound_play_open(8000, paFloat32, 320, 1, NULL, NULL);
    rksound_record_open(16000, paInt16, 320, 4, record_callback, NULL);
  
    AiConfigs ai_configs("localai.json");
    
    HuoshanProto huoshan(getCurrentTimestamp(), ai_configs.GetJson()["system"]["prompt"].dump(), ai_configs.GetJson()["system"]["hello"].get<std::string>());
    local_ai.LoadAction(ai_configs.GetJson());
    LOGL(TAG);
    std::thread t([&huoshan]()
    {
        while(1)
        {
            auto audio = apool.get_audio();
            if(audio.empty())
            {
                huoshan.play_idle++;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            huoshan.play_idle = 0;
            //printf("Audio: play %d bytes\n", audio.size());
            rksound_play_pcm(audio.data(), audio.size()/4);
        }
    });
    LOGL(TAG);

    WssClient client("openspeech.bytedance.com/api/v3/realtime/dialogue", false);
    client.auto_reconnect = true;
    client.config.header.insert({"X-Api-App-ID", "1279857841"});
    client.config.header.insert({"X-Api-Access-Key", "vqIUYu8VverjejYLroLhwlhH1VSk-XSY"});
    client.config.header.insert({"X-Api-Resource-Id", "volc.speech.dialog"});
    client.config.header.insert({"X-Api-App-Key", "PlgvMymc7f3tQnJ6"});
    client.config.header.insert({"X-Api-Connect-Id", "xdrobot"});
    client.on_message = [&huoshan, &lcm](shared_ptr<WssClient::Connection> connection, shared_ptr<WssClient::InMessage> in_message)
    {
        //cout << "Client: Message received: \"" << in_message->string() << "\"" << endl;
        HuoshanProto h = huoshan.parse(in_message->string());
        if(h.optional.event == Event::ConnectionStarted)
        {
            connection->send(huoshan.StartSession());
        }
        if(h.optional.event == Event::SessionStarted)
        {
            connection->send(huoshan.SayHello());
        }
        if(h.optional.event == Event::ASRResponse)
        {
            last_asr = h.payload;
        }
        if(h.optional.event == Event::ASREnded)
        {
            try
            {
                nlohmann::json j = nlohmann::json::parse(last_asr);
                std::string tts = j["extra"]["origin_text"];
                LOGD(TAG, "ASR: {}", tts.c_str());
                auto action = local_ai.MatchAction(tts);
                if(!action.name.empty())
                {
                    mars_message::String msg, ret;
                    msg.value = action.cmd.params;
                    if(lcm.send(action.cmd.function, &msg, &ret, 500, 1) == 0)
                    {
                        if(ret.value != "")
                        {
                            connection->send(huoshan.ChatTTSText(ret.value, true, false));
                            connection->send(huoshan.ChatTTSText("", false, true));
                        }
                        else
                        {
                            connection->send(huoshan.ChatTTSText(RandReply(action.replysp), true, false));
                            connection->send(huoshan.ChatTTSText("", false, true));
                        }
                    }
                    else
                    {
                        connection->send(huoshan.ChatTTSText(RandReply(action.replysn), true, false));
                        connection->send(huoshan.ChatTTSText("", false, true));
                    }
                    huoshan.disabled_remote = true;
                }
            }
            catch(const std::exception& e)
            {
                LOGE(TAG, "ASR Parse Error: {}", e.what());
                LOGD(TAG, "ASR Raw Data: {}", last_asr.c_str());
            }
        }
        if(h.optional.event == Event::TTSEnded)
        {
            if(huoshan.disabled_remote)
            {
                LOGD(TAG, "HS: TTS ended, reset remote");
                huoshan.disabled_remote = false;
            }
        }
    };

    client.on_open = [&huoshan](shared_ptr<WssClient::Connection> connection)
    {
        cout << "Client: Opened connection" << endl;
        LOGL(TAG);
        connection->send(huoshan.StartConnect());
    };

    client.on_close = [&huoshan](shared_ptr<WssClient::Connection> /*connection*/, int status, const string & /*reason*/)
    {
        LOGD(TAG, "Client: Closed connection with status code {}", status);
        huoshan.is_ready = false;
    };

    // See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for error code meanings
    client.on_error = [&huoshan](shared_ptr<WssClient::Connection> /*connection*/, const SimpleWeb::error_code &ec)
    {
        LOGD(TAG, "Client: error message: {}", ec.message());
        huoshan.is_ready = false;
    };

    std::thread mic_thread([&client, &huoshan]()
    {
        while(1)
        {
            auto audio = micpool.get_audio();
            if(audio.empty())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            // Send audio to server
            if(huoshan.is_ready == false || huoshan.play_idle < 50)
            {
                //printf("HS: session not started, skip audio\n");
                continue;
            }
            client.send(huoshan.TaskRequest((uint8_t*)audio.data(), audio.size()));
        }
    });

    client.start_nonblock();
    while(true)
    {
        client.poll();
        lcm.handleTimeout(1);
    }
}
#else
#include "HuoshanEngine.hpp"
#include "aixd.h"
int main(int argc, char *argv[])
{
    lcm::LCM lcm;

    if (!lcm.good())
    {
        LOGE(TAG, "LCM no good");
        return 1;
    }
    PlayDev playDev(8000, ma_format_f32, 320, 1);
    playDev.Open();
    RecordDev recordDev(8000, ma_format_s16, 320, 1);
    recordDev.Open();
#if 0   
    AiConfigs ai_configs("localai.json");
    LOGL(TAG);
    LocalAi local_ai;
    local_ai.LoadAction(ai_configs["actions"]);
    LOGL(TAG);
    HuoshanEngine engine("openspeech.bytedance.com/api/v3/realtime/dialogue", false,
        ai_configs["system"]["prompt"].dump(),
        ai_configs["system"]["hello"].get<std::string>(),
        &lcm, &playDev, &recordDev, &local_ai);

    engine.Connect(false);
    while(true)
    {
        engine.Poll();
        lcm.handleTimeout(10);
    }
#else
    AiSoundTask(lcm, playDev, recordDev);
#endif
    playDev.Close();
    recordDev.Close();
    return 0;
}
#endif
/*
[
    {
        "name":"记住当前地点",
        "patterns":["111","222"],
        "cmd":{
            "function":"",
            "param":"{}"
        },
        "replys":["",""]
    }
]


*/