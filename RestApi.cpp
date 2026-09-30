#include "RestApi.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <gio/gio.h>
#include <jansson.h>

#include "glib.h"

#include "Log.h"


using namespace rest;
using namespace http;

namespace
{

const auto Log = SignalingServerLog;
#define TAG "[REST]" " "

enum {
    CREDENTIALS_UNIQUE_IP_LIMIT = 10,
    CREDENTIALS_UNIQUE_IP_LIMIT_RESET_INTERVAL = 60,
};

inline constexpr std::string_view CredentialsPrefix = "/credentials";

namespace ContentType
{
    const char *const ApplicationJson = "application/json";
}

typedef char* json_char_ptr;
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(json_char_ptr, free, nullptr)

inline char* json_dumps(json_t* json) noexcept
{
    return ::json_dumps(json, JSON_INDENT(4));
}

inline std::pair<StatusCode, MHD_Response*>
ApplyJSONHeader(std::pair<StatusCode, MHD_Response*>&& response) noexcept
{
    if(response.second) {
        MHD_add_response_header(
            response.second,
            MHD_HTTP_HEADER_CONTENT_TYPE,
            ContentType::ApplicationJson);
    }

    return std::move(response);
}

inline std::pair<StatusCode, MHD_Response*>
ApplyOptionsHeaders(std::pair<StatusCode, MHD_Response*>&& response) noexcept
{
    if(response.second) {
        MHD_add_response_header(
            response.second,
            MHD_HTTP_HEADER_ACCESS_CONTROL_ALLOW_HEADERS,
            "content-type");
    }

    return std::move(response);
}

std::string ClientIp(MHD_Connection* connection) noexcept
{
    const MHD_ConnectionInfo* connectionInfo =
        MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);

    if(!connectionInfo->client_addr)
        return {};

    char publicIp[INET6_ADDRSTRLEN];
    switch(connectionInfo->client_addr->sa_family) {
        case AF_INET: {
            const sockaddr_in* in = reinterpret_cast<const sockaddr_in*>(connectionInfo->client_addr);
            if(!inet_ntop(in->sin_family, &in->sin_addr, publicIp, sizeof(publicIp)))
                return {};

            break;
        }
        case AF_INET6: {
            const sockaddr_in6* in6 = reinterpret_cast<const sockaddr_in6*>(connectionInfo->client_addr);
            if(!inet_ntop(in6->sin6_family, &in6->sin6_addr, publicIp, sizeof(publicIp)))
                return {};

            break;
        }
    }

    return publicIp;
}

std::string XRealClientIp(MHD_Connection* connection) noexcept
{
    const char* xRealIp = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "X-Real-IP");

    if(!xRealIp)
        return {};

    return xRealIp;
}

std::string ClientIpLogString(MHD_Connection* connection) noexcept
{
    const std::string ip = ClientIp(connection);
    const std::string xRealIp = XRealClientIp(connection);

    if(xRealIp.empty())
        return std::format("IP: {}", ip);
    else
        return std::format("IP: {}, X-Real-IP: {}", ip, xRealIp);
}

bool IsRateLimitExceeded(Context* context, const std::string& ip) noexcept
{
    const auto now = std::chrono::steady_clock::now();

    auto it = context->ipHistory.end();
    if(now - context->ipHistoryTimestamp > std::chrono::seconds(CREDENTIALS_UNIQUE_IP_LIMIT_RESET_INTERVAL)) {
        context->ipHistory.clear();
        context->ipHistoryTimestamp = now;
    } else {
        it = context->ipHistory.find(ip);
    }

    if(it != context->ipHistory.end()) {
        Log()->info(TAG "Too many requests from {}. Access denied", ip);
        return true;
    } else if(context->ipHistory.size() < CREDENTIALS_UNIQUE_IP_LIMIT) {
        context->ipHistory.emplace(std::move(ip), 1);
        return false;
    } else {
        Log()->info(TAG "Too many requests. Access from {} denied", ip);
        return true;
    }
}

bool IsRateLimitExceeded(Context* context, MHD_Connection* connection) noexcept
{
    const std::string clientIp = ClientIp(connection);
    if(clientIp.empty()) {
        Log()->warn(TAG "Failed to get client IP. Access denied.");
        return true;
    }

    g_autoptr(GInetAddress) clientIpAddr = g_inet_address_new_from_string(clientIp.c_str());
    if(!clientIpAddr)
        return true;

    const bool isClientIpLocal = g_inet_address_get_is_loopback(clientIpAddr) ||
        g_inet_address_get_is_site_local(clientIpAddr);

    const std::string xRealClientIp = XRealClientIp(connection);

    if(!isClientIpLocal || xRealClientIp.empty())
        return IsRateLimitExceeded(context, clientIp);

    return IsRateLimitExceeded(context, xRealClientIp);
}

std::pair<http::StatusCode, MHD_Response*> HandleCredentialsRequest(
    Context* context,
    MHD_Connection* connection,
    http::Method method,
    std::string_view path,
    std::string_view body) noexcept
{
    if(method != http::Method::POST)
        return BadRequest();

    if(!path.empty())
        return BadRequest();

    if(IsRateLimitExceeded(context, connection))
        return TooManyRequests();

    json_auto_t* requestJson = json_loadb(
        body.data(),
        body.size(),
        0,
        nullptr);
    if(!requestJson || !json_is_object(requestJson))
        return BadRequest();

    json_t* clientIdJson = json_object_get(requestJson, "client_id");
    if(!clientIdJson)
        return BadRequest();

    if(!json_is_string(clientIdJson))
        return BadRequest();

    const char* clientId = json_string_value(clientIdJson);
    if(!clientId)
        return BadRequest();

    std::optional<AgentsDb::AgentCredentials> agentCredentials =
        context->agentsDb.registerAgent(clientId);
    if(!agentCredentials)
        return InternalError();

    if(!agentCredentials) {
        Log()->error(TAG "Failed to register Agent. Client Id: {}, {}",
            clientId,
            ClientIpLogString(connection));
        return InternalError();
    }

    Log()->info(TAG "New Agent registered. Client Id: {}, Agent Id: {}, {}",
        clientId,
        agentCredentials->agentId,
        ClientIpLogString(connection));

    json_auto_t* responseJson = json_object();
    json_object_set_new(
        responseJson,
        "agent_id",
        json_string(agentCredentials->agentId .c_str()));
    json_object_set_new(
        responseJson,
        "access_token",
        json_string(agentCredentials->accessToken.c_str()));

    g_auto(json_char_ptr) responseBody = json_dumps(responseJson);
    if(!responseBody)
        return InternalError();

    MHD_Response* response = MHD_create_response_from_buffer(
        strlen(responseBody),
        responseBody,
        MHD_RESPMEM_MUST_FREE);
    if(!response)
        return InternalError();

    responseBody = nullptr; // to avoid double free, since response owns it now

    return ApplyJSONHeader(Created(response));
}

}

std::pair<http::StatusCode, MHD_Response*> rest::HandleApiRequest(
    Context* context,
    MHD_Connection* connection,
    http::Method method,
    const char* uri,
    std::string_view body) noexcept
{
    if(!uri)
        return InternalError();

    g_autofree gchar* path = nullptr;
    if(!g_uri_split(
        uri,
        G_URI_FLAGS_NONE,
        nullptr, //scheme
        nullptr, //userinfo
        nullptr, //host
        nullptr, //port
        &path,
        nullptr, //query
        nullptr, //fragment
        nullptr))
    {
        return InternalError();
    }

    std::string_view pathView(path);

    if(!pathView.starts_with(ApiPrefix))
        return BadRequest();

    pathView.remove_prefix(ApiPrefix.size());

    if(pathView.starts_with(CredentialsPrefix)) {
        pathView.remove_prefix(CredentialsPrefix.size());
        return HandleCredentialsRequest(context, connection, method, pathView, body);
    }

    return BadRequest();
}
