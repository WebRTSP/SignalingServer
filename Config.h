#pragma once

#include <spdlog/common.h>

#include "Signalling/Config.h"


struct Config: public WsServerConfig
{
    spdlog::level::level_enum logLevel = spdlog::level::info;
    spdlog::level::level_enum lwsLogLevel = spdlog::level::warn;

    std::string stunServer = "stun://stun.freeswitch.org:3478"; // to detect own public IP

    std::optional<std::string> publicIp;

    std::optional<std::string> turnStaticAuthSecret;
};
