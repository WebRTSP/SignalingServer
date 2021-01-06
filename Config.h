#pragma once

#include <string>
#include <map>

#include <spdlog/common.h>

#include "Http/Config.h"


typedef std::map<const std::string, const std::string> AuthTokens;

struct Config
{
    spdlog::level::level_enum logLevel = spdlog::level::info;
    spdlog::level::level_enum lwsLogLevel = spdlog::level::warn;

    http::Config httpConfig;

    std::string serverName;
    std::string certificate;
    std::string key;

    unsigned short frontPort;
    unsigned short secureFrontPort;

    unsigned short backPort;
    unsigned short secureBackPort;

    bool bindToLoopbackOnly = false;

    std::string stunServer;

    std::string turnServer;
    std::string turnUsername;
    std::string turnCredential;
    std::string turnStaticAuthSecret;
    unsigned turnPasswordTTL = 24 * 60 * 60;

    std::string turnsServer;
    std::string turnsUsername;
    std::string turnsCredential;
    std::string turnsStaticAuthSecret;
    unsigned turnsPasswordTTL = 24 * 60 * 60;

    AuthTokens backAuthTokens;
};
