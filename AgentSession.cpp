#include "AgentSession.h"

#include "RtspParser/RtspParser.h"
#include "RtspParser/RtspSerialize.h"

#include "Helpers/TurnRestApi.h"


AgentSession::AgentSession(
    const Config* config,
    SharedData* sharedData,
    std::string&& agentId,
    const SendRequest& sendRequest,
    const SendResponse& sendResponse) noexcept :
    rtsp::Session(sendRequest, sendResponse),
    rtsp::MessageForwardMixin(rtsp::MessageForwardMixin::SessionType::Agent, this),
    _config(config),
    _sharedData(sharedData),
    _agentId(std::move(agentId))
{
    // multiple stale sessions are possible,
    // but only last one can be active
    _sharedData->agentsSessions[_agentId] = this;
}

AgentSession::~AgentSession() noexcept
{
    // it can be stale session
    auto it = _sharedData->agentsSessions.find(_agentId);
    if(it != _sharedData->agentsSessions.end() && it->second == this) {
        _sharedData->agentsSessions.erase(it);
    }
}

bool AgentSession::onGetParameterRequest(std::unique_ptr<rtsp::Request>&& requestPtr) noexcept
{
    const std::string& contentType = requestPtr->contentType;

    if(contentType.empty() && requestPtr->body.empty()) {
        // PING/PONG case
        sendOkResponse(requestPtr->cseq);
        return true;
    }

    if(contentType != rtsp::TextParametersContentType)
        return false;

    rtsp::ParametersNames names;
    if(!rtsp::ParseParametersNames(requestPtr->body, &names))
        return false;

    auto nameIt = names.find("ice-servers");
    if(names.end() == nameIt)
        return false;

    rtsp::Parameters parameters;
    if(_config->publicIp && _config->turnStaticAuthSecret) {
        const std::string coturnEndpoint = *_config->publicIp + ":" + std::to_string(TURN_DEFAULT_PORT);
        parameters.emplace("stun-server",
            "stun://" + coturnEndpoint);
        parameters.emplace("turn-server",
            GenerateTURNServerUrl(
                requestPtr->uri,
                std::chrono::seconds(TURN_TEMP_PASSWORD_DEFAULT_TTL),
                _config->turnStaticAuthSecret.value(),
                coturnEndpoint,
                false));
    }

    std::string body;
    rtsp::Serialize(parameters, &body);

    sendOkResponse(
        requestPtr->cseq,
        rtsp::MediaSessionId(),
        rtsp::TextParametersContentType,
        std::move(body));

    return true;
}

bool AgentSession::handleRequest(std::unique_ptr<rtsp::Request>&& requestPtr) noexcept
{
    switch(requestPtr->method) {
    case rtsp::Method::GET_PARAMETER:
        return onGetParameterRequest(std::move(requestPtr));
    default:
        return forwardMediaSessionRequest(std::move(requestPtr));
    }
}

bool AgentSession::handleResponse(
    const rtsp::Request& request,
    std::unique_ptr<rtsp::Response>&& responsePtr) noexcept
{
    if(std::optional<bool> response = tryForwardResponse(request, std::move(responsePtr)))
        return response.value();

    return false;
}
