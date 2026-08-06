#include "Session.h"

#include "RtspParser/RtspParser.h"

#include "AgentsDb.h"
#include "AgentSession.h"


Session::Session(
    SharedData* sharedData,
    const SendRequest& sendRequest,
    const SendResponse& sendResponse) noexcept :
    rtsp::Session(sendRequest, sendResponse),
    rtsp::MessageForwardMixin(rtsp::MessageForwardMixin::SessionType::Regular, this),
    _sharedData(sharedData)
{
}

Session::~Session() noexcept
{
}

bool Session::isAgentUri(const std::string& uri)
{
    return !uri.empty() && uri.front() == AgentsDb::AGENT_ID_PREFIX;
}

bool Session::handleRequest(
    std::unique_ptr<rtsp::Request>&& requestPtr) noexcept
{
    if(requestPtr->uri == rtsp::WildcardUri) {
        return rtsp::Session::handleRequest(std::move(requestPtr));
    } else if(!isRequestToAgent(requestPtr)) {
        sendNotFoundResponse(requestPtr->cseq);
        return true;
    }

    auto [streamerName, substreamName] = rtsp::SplitUri(requestPtr->uri);

    auto agentSessionIt = _sharedData->agentsSessions.find(streamerName);
    if(agentSessionIt == _sharedData->agentsSessions.end()) {
        sendNotFoundResponse(requestPtr->cseq);
        return true;
    }

    return MessageForwardMixin::forwardRequest(
        std::move(requestPtr),
        std::string(!substreamName.empty() ? substreamName : rtsp::WildcardUri),
        agentSessionIt->second);
}

bool Session::onGetParameterRequest(std::unique_ptr<rtsp::Request>&& requestPtr) noexcept
{
    const std::string& contentType = requestPtr->contentType;

    if(contentType.empty() && requestPtr->body.empty()) {
        // PING/PONG case
        sendOkResponse(requestPtr->cseq);
        return true;
    }

    return false;
}

bool Session::handleResponse(
    const rtsp::Request& request,
    std::unique_ptr<rtsp::Response>&& responsePtr) noexcept
{
    if(std::optional<bool> response = tryForwardResponse(request, std::move(responsePtr)))
        return response.value();

    return false;
}
