#pragma once

#include <unordered_map>

#include "AgentsDb.h"


class Session;
class AgentSession;
struct SessionsSharedData {
    struct Hash {
        using is_transparent = void;
        size_t operator() (const char* key) const noexcept
            { return std::hash<std::string_view>()(key); }
        size_t operator() (std::string_view key) const noexcept
            { return std::hash<std::string_view>()(key); }
        size_t operator() (const std::string& key) const noexcept
            { return std::hash<std::string>()(key); }
    };

    std::unordered_map<std::string, AgentSession*, Hash, std::equal_to<>> agentsSessions;
    AgentsDb agentsDb;
};
