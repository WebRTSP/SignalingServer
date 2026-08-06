#pragma once

#include "RtspSession/Session.h"
#include "RtspSession/MessageForwardMixin.h"

#include "SessionsSharedData.h"


class Session : public rtsp::Session, public rtsp::MessageForwardMixin
{
public:
    typedef SessionsSharedData SharedData;

    Session(
        SharedData*,
        const SendRequest&,
        const SendResponse&) noexcept;
    ~Session() noexcept override;

private:
    bool isAgentUri(const std::string& uri);
    bool isRequestToAgent(const rtsp::Request& request)
        { return isAgentUri(request.uri); }
    bool isRequestToAgent(const std::unique_ptr<rtsp::Request>& requestPtr)
        { return isRequestToAgent(*requestPtr); }

    bool handleRequest(
        std::unique_ptr<rtsp::Request>&&) noexcept override;
    bool onGetParameterRequest(std::unique_ptr<rtsp::Request>&&) noexcept override;

    bool handleResponse(
        const rtsp::Request&,
        std::unique_ptr<rtsp::Response>&&) noexcept override;

private:
    SharedData *const _sharedData;
};
