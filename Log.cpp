#include "Log.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_sinks.h>


static std::shared_ptr<spdlog::logger> Logger;

void InitSignalingServerLogger(spdlog::level::level_enum level)
{
    spdlog::sink_ptr sink = std::make_shared<spdlog::sinks::stdout_sink_st>();

    Logger = std::make_shared<spdlog::logger>("SignalingServer", sink);

    Logger->set_level(level);
}

const std::shared_ptr<spdlog::logger>& SignalingServerLog()
{
    if(!Logger)
#ifndef NDEBUG
        InitSignalingServerLogger(spdlog::level::debug);
#else
        InitSignalingServerLogger(spdlog::level::info);
#endif

    return Logger;
}
