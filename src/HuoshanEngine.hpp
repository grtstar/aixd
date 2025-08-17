#include <stdint.h>
#include <mutex>
#include <string>
#include <thread>
#include <chrono> 
#include <regex> 
#include <fstream>
#define SIMPLEWEB_USE_STANDALONE_ASIO 1
#define ASIO_USE_TS_EXECUTOR_AS_DEFAULT  1
#include "simpleweb/wss_client.hpp"
#include "json.hpp"
#include "sound.hpp"
#include "log_.h"

#include <mars_message/String.hpp>

#define TAG "HUOSHAN"

using SimpleWeb::WssClient;

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
    std::string asrText;

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
        LOGD(TAG, "HS: StartConnect");
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
    std::string TaskRequest(const void* audio, size_t len)
    {
        //printf("HS: TaskRequest\n");
        auto d = genrate_header(0x1, AUDIO_ONLY_REQ, MSG_WITH_EVENT, NO_SERIALIZATION);
        d += to_bytesb(Event::TaskRequest);
        
        d += to_bytesb(session_id.length());
        d += session_id;

        auto payload_size = to_bytesb(len);
        d += to_bytesb(len); // payload size
        d.insert(d.end(), (const uint8_t*)audio, (const uint8_t*)audio + len); // payload
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
    operator nlohmann::json &()
    {
        return j;
    }
    nlohmann::json & operator[](const std::string &key)
    {
        return j[key];
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

class LocalAi
{
    std::vector<LocalAction> actions;
public:
    bool LoadAction(nlohmann::json & j)
    {
        try
        {
            for(auto & action : j)
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

// upload voice int16, 16k, monophonic, 1 channel
// download voice float32, 24k, monophonic, 1 channel
// or int16 24k mono, or ogg/opus
class HuoshanEngine
{
    WssClient client;
    HuoshanProto proto;
    lcm::LCM *lcm;
    PlayDev *playDev;
    RecordDev *recordDev;
    LocalAi *local_ai;
    PcmConverter pcm_converter;
    AudioQueue apool;
public:
    std::string GetSessionId()
    {
        // Get MAC address of eth0
        std::ifstream ifs("/sys/class/net/eth0/address");
        std::string mac;
        if (ifs.good()) {
            std::getline(ifs, mac);
            // Remove colons from MAC address
            mac.erase(std::remove(mac.begin(), mac.end(), ':'), mac.end());
        }
        return mac;
    }
    HuoshanEngine(const char* url, bool verify_certificate, 
        std::string prompt, std::string hello,
        lcm::LCM *lcm, PlayDev *pDev, RecordDev *rDev, LocalAi *local_ai) : client(url, verify_certificate),
                  proto(GetSessionId(), prompt, hello),
                  lcm(lcm),
                  playDev(pDev),
                  recordDev(rDev),
                  local_ai(local_ai),
                    pcm_converter(ma_format_f32, 24000, 1, ma_format_f32, 8000, 1)
    {
        // Constructor implementation
        playDev->AddCb([](void *pUserdata, 
                    void *pOutput, 
                    const void *pInput, 
                    ma_uint32 frameCount){
                    HuoshanEngine *engine = static_cast<HuoshanEngine *>(pUserdata);
                    auto audio = engine->apool.pop_front(PcmConverter::GetBytesPerFrame(ma_format_f32, 1) * frameCount);
                    if(audio.empty())
                    {
                        return;
                    }
                    engine->proto.play_idle = 0;
                    memcpy(pOutput, audio.data(), audio.size());
                    return;

        }, this);
        recordDev->AddCb([](void *pUserdata, 
                    void *pOutput, 
                    const void *pInput, 
                    ma_uint32 frameCount) {
                    HuoshanEngine *engine = static_cast<HuoshanEngine *>(pUserdata);
                    engine->client.send(engine->proto.TaskRequest(pInput, frameCount * engine->recordDev->BytesPerFrame()));
                    return;
        }, this);
    }
    void Connect(bool blocking = true)
    {
        // Connect to the Huoshan server
        client.auto_reconnect = true;
        client.config.header.insert({"X-Api-App-ID", "1279857841"});
        client.config.header.insert({"X-Api-Access-Key", "vqIUYu8VverjejYLroLhwlhH1VSk-XSY"});
        client.config.header.insert({"X-Api-Resource-Id", "volc.speech.dialog"});
        client.config.header.insert({"X-Api-App-Key", "PlgvMymc7f3tQnJ6"});
        client.config.header.insert({"X-Api-Connect-Id", "xdrobot"});
        client.on_message = [this](std::shared_ptr<WssClient::Connection> connection, std::shared_ptr<WssClient::InMessage> in_message)
        {
            // Handle incoming messages
            HandleResponse(connection, in_message->string());
        };
        client.on_open = [this](std::shared_ptr<WssClient::Connection> connection)
        {
            // Handle connection open event
            connection->send(proto.StartConnect());
        };
        client.on_close = [this](std::shared_ptr<WssClient::Connection> /*connection*/, int status, const std::string & /*reason*/)
        {
            // Handle connection close event
        };
        client.on_error = [this](std::shared_ptr<WssClient::Connection> /*connection*/, const SimpleWeb::error_code &ec)
        {
            // Handle connection error
            LOGD(TAG, "Client: error message: {}", ec.message());
        };
        if(blocking)
        {
            client.start();
        }
        else
        {
            client.start_nonblock();
        }
    }
    void Poll() 
    {
        // Poll the client for incoming messages
        client.poll();
    }
    void TTS(const std::string & text)
    {

    }

    std::string RandReply(std::vector<std::string> replys)
    {
        if(replys.empty())
            return "";
        int idx = rand() % replys.size();
        return replys[idx];
    }

    void HandleResponse(std::shared_ptr<WssClient::Connection> connection, const std::string & response)
    {
        HuoshanProto h = proto.parse(response);
        if(h.optional.event == Event::ConnectionStarted)
        {
            connection->send(proto.StartSession());
        }
        if(h.optional.event == Event::SessionStarted)
        {
            connection->send(proto.SayHello());
        }
        if(h.optional.event == Event::ASRResponse)
        {
            proto.asrText = h.payload;
        }
        if(h.optional.event == Event::ASREnded)
        {
            try
            {
                nlohmann::json j = nlohmann::json::parse(proto.asrText);
                std::string tts = j["extra"]["origin_text"];
                LOGD(TAG, "ASR: {}", tts.c_str());
                auto action = local_ai->MatchAction(tts);
                if(!action.name.empty())
                {
                    mars_message::String msg, ret;
                    msg.value = action.cmd.params;
                    if(lcm->send(action.cmd.function, &msg, &ret, 500, 1) == 0)
                    {
                        if(ret.value != "")
                        {
                            connection->send(proto.ChatTTSText(ret.value, true, false));
                            connection->send(proto.ChatTTSText("", false, true));
                        }
                        else
                        {
                            connection->send(proto.ChatTTSText(RandReply(action.replysp), true, false));
                            connection->send(proto.ChatTTSText("", false, true));
                        }
                    }
                    else
                    {
                        connection->send(proto.ChatTTSText(RandReply(action.replysn), true, false));
                        connection->send(proto.ChatTTSText("", false, true));
                    }
                    proto.disabled_remote = true;
                }
            }
            catch(const std::exception& e)
            {
                LOGE(TAG, "ASR Parse Error: {}", e.what());
                LOGD(TAG, "ASR Raw Data: {}", proto.asrText);
            }
        }
        if(h.optional.event == Event::TTSEnded)
        {
            if(proto.disabled_remote)
            {
                LOGD(TAG, "HS: TTS ended, reset remote");
                proto.disabled_remote = false;
            }
        }
        if(h.header.message_type == AUDIO_ONLY_RSP)
        {
            // printf("HS: audio: %d\n", hp.payload_size);
            // Convert 24000Hz Float32 audio to 8000Hz by taking every third sample

            if(!proto.disabled_remote)
            {
                std::vector<uint8_t> audio = pcm_converter.Convert(h.payload.data(), h.payload.size());
                if(!audio.empty())
                {
                    apool.push(audio);
                }
            }
        }
    }
};