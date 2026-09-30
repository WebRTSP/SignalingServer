#pragma once

#include <unordered_set>

#include "Http/HttpMicroServer.h"

#include "AgentsDb.h"


namespace rest
{

inline constexpr std::string_view ApiPrefix = "/api";

struct Context
{
    // it's more efficient to have multipe connections to the same db
    // comparing to protection with mutex
    AgentsDb agentsDb;
    std::unordered_set<std::string> ipHistory;
    std::chrono::steady_clock::time_point ipHistoryTimestamp;
};

std::pair<http::StatusCode, MHD_Response*> HandleApiRequest(
    Context*,
    MHD_Connection*,
    http::Method,
    const char* uri,
    std::string_view body) noexcept;

}
