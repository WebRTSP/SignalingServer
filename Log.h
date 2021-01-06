#pragma once

#include <memory>

#include <spdlog/spdlog.h>


void InitSignalingServerLogger(spdlog::level::level_enum level);

const std::shared_ptr<spdlog::logger>& SignalingServerLog();
