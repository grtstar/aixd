#include <lcm/lcm-cpp.hpp>
#include "HuoshanEngine.hpp"

void AiSoundTask(lcm::LCM &lcm, PlayDev &playDev, RecordDev &recordDev)
{
    AiConfigs ai_configs("localai.json");
    LOGL(TAG);    
    LocalAi local_ai;
    local_ai.LoadAction(ai_configs["actions"]);
    LOGL(TAG);    
    HuoshanEngine engine("openspeech.bytedance.com/api/v3/realtime/dialogue", false,
        ai_configs["system"]["prompt"].dump(),
        ai_configs["system"]["hello"].get<std::string>(),
        &lcm, &playDev, &recordDev, &local_ai);
    engine.Connect();
}
