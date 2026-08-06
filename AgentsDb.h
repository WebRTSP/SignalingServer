#pragma once

#include <memory>
#include <optional>

#include <Signalling/WsServer.h>


class AgentsDb final : public WsServer::AgentsDb
{
public:
    enum: std::string::value_type {
        AGENT_ID_PREFIX = '~'
    };

    AgentsDb(const AgentsDb&) = delete;
    AgentsDb(AgentsDb&&) = delete;
    AgentsDb& operator = (const AgentsDb&) = delete;
    AgentsDb& operator = (AgentsDb&&) = delete;

    AgentsDb() noexcept;
    ~AgentsDb() noexcept;

    bool isOpen() const noexcept;
    std::optional<AgentCredentials>
        registerAgent(const std::string& clientId) noexcept override;
    bool authenticateAgent(
        const std::string& clientId,
        const std::string& agentId,
        const std::string& accessToken) noexcept override;
    bool isRegistered(const std::string& agentId) const noexcept;

private:
    struct Private;
    std::unique_ptr<Private> _p;
};
