#include <lcm/lcm-cpp.hpp>
#include "HuoshanEngine.hpp"

void AiTask(lcm::LCM &lcm, PlayDev &playDev, RecordDev &recordDev)
{
    AiConfigs ai_configs("localai.json");
    LocalAi local_ai;
    local_ai.LoadAction(ai_configs.GetJson()["actions"]);
    HuoshanEngine engine("openspeech.bytedance.com/api/v3/realtime/dialogue", false
        , ai_configs.GetJson()["system"]["session_id"].get<std::string>(),
        ai_configs.GetJson()["system"]["prompt"].dump(),
        ai_configs.GetJson()["system"]["hello"].get<std::string>(),
        &lcm, &playDev, &recordDev, &local_ai);
    engine.Connect();
}
