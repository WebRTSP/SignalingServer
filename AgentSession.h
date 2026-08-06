#pragma once

#include "RtspSession/Session.h"
#include "RtspSession/MessageForwardMixin.h"

#include "Config.h"
#include "SessionsSharedData.h"


class AgentSession : public rtsp::Session, public rtsp::MessageForwardMixin
{
public:
    typedef SessionsSharedData SharedData;

    AgentSession(
        const Config*,
        SharedData*,
        std::string&& agentId,
        const SendRequest&,
        const SendResponse&) noexcept;
    ~AgentSession() noexcept;

protected:
    bool onGetParameterRequest(std::unique_ptr<rtsp::Request>&&) noexcept override;
    bool handleRequest(std::unique_ptr<rtsp::Request>&&) noexcept override;

    bool handleResponse(
        const rtsp::Request&,
        std::unique_ptr<rtsp::Response>&&) noexcept override;

private:
    const Config *const _config;
    SharedData *const _sharedData;
    const std::string _agentId;
};
