#pragma once
#include <unistd.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/details/os.h>
#include <spdlog/fmt/bin_to_hex.h>
using spdlog::details::os::create_dir;
class MarsLog
{
    std::shared_ptr<spdlog::logger> logger;

public:
    enum
    {
        LOG_VERBOSE,
        LOG_DEBUG,
        LOG_WARN,
        LOG_ERROR,
        LOG_CRITICAL,
        LOG_OFF
    };

    MarsLog(bool enableConsole, bool enableFile)
    {
        logger = std::make_shared<spdlog::logger>("log");
        // spdlog::register_logger(logger);
        if (enableConsole)
        {
            auto console = std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>();
            logger->sinks().push_back(console);
        }
        if (enableFile)
        {
            create_dir("/tmp/xdlogs");
            std::string path = "/tmp/xdlogs/";
            path += GetProcessName();
            path += ".log";
            auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path, 1024*1024, 2);
            logger->sinks().push_back(file);
        }

        logger->flush_on(spdlog::level::debug);
        logger->set_pattern("[%Y-%m-%d %H:%M%S.%e] %^[%L]%$ %v");

        SetLevel(LOG_VERBOSE);
    }
    ~MarsLog()
    {
        logger->flush();
    }
    void SetLevel(int level)
    {
        switch (level)
        {
        case LOG_VERBOSE:
            logger->set_level(spdlog::level::trace);
            break;
        case LOG_DEBUG:
            logger->set_level(spdlog::level::debug);
            break;
        case LOG_WARN:
            logger->set_level(spdlog::level::warn);
            break;
        case LOG_ERROR:
            logger->set_level(spdlog::level::err);
            break;
        case LOG_CRITICAL:
            logger->set_level(spdlog::level::critical);
            break;
        case LOG_OFF: // pass through
            logger->set_level(spdlog::level::off);
        default:
            break;
        }
    }

    static std::string GetProcessName()
    {
        char strProcessPath[1024] = {0};
        if(readlink("/proc/self/exe", strProcessPath,1023) <=0)
        {
            return std::string("NONAME");
        }
        char *strProcessName = strrchr(strProcessPath, '/');
        if(!strProcessName)
        {
            return std::string(strProcessName);
        }
        else
        {
            return std::string(++strProcessName);
        }
    }

    static MarsLog* LoggerInstance()
    {
        static MarsLog log(true, true);
        return &log;
    }
    std::shared_ptr<spdlog::logger> Logger()
    {
        return logger;
    }

    static void LogVerison(const char* version)
    {
        char buf[256] = {0};
        std::string procName = GetProcessName();
        LoggerInstance()->Logger()->debug("(VER) {} {} build:{}_{}", procName, version, __DATE__, __TIME__);
        sprintf(buf, "/tmp/xdlogs/%s.ver", procName.c_str());
        FILE *fp = fopen(buf, "w");
        if(fp)
        {
            fprintf(fp, "%s %s build:%s_%s\n", procName.c_str(), version, __DATE__, __TIME__);
            fflush(fp);
            fclose(fp);
        }
    }
};

#define LOGV(TAG, ...) MarsLog::LoggerInstance()->Logger()->trace("(" TAG ") " __VA_ARGS__)
#define LOGD(TAG, ...) MarsLog::LoggerInstance()->Logger()->debug("(" TAG ") " __VA_ARGS__)
#define LOGW(TAG, ...) MarsLog::LoggerInstance()->Logger()->warn("(" TAG ") " __VA_ARGS__)
#define LOGE(TAG, ...) MarsLog::LoggerInstance()->Logger()->error("(" TAG ") " __VA_ARGS__)
#define LOGC(TAG, ...) MarsLog::LoggerInstance()->Logger()->critical("(" TAG ") " __VA_ARGS__)
#define LOGL(TAG)      LOGV(TAG, "{} {} ------------", __FUNCTION__, __LINE__)
#define LOGLT          LOGL(TAG)
#define LOGVERSION(ver)     MarsLog::LogVerison(ver)
#define LOGD_TIME(TAG, interval, ...) \
    do                                \
    {                                 \
        static uint64_t last = 0;     \
        uint64_t now = TimeTick::Ms();\
        if (now - last > interval)    \
        {                             \
            last = now;               \
            LOGD(TAG, __VA_ARGS__);  \
        }                             \
    } while (0)
#define LOGD_MONITOR(interval_min, interval_max, fnname) \
    do                                \
    {                                 \
        static uint64_t last = 0;     \
        uint64_t now = TimeTick::Ms();\
        if (last != 0 && (now - last < interval_min || now - last > interval_max))    \
        {                             \
            LOGW("Monitor", "{} interval is {}", fnname, now-last);  \
        }                           \
        last = now;               \
    } while (0)
    