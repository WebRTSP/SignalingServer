#include "SignalingServer.h"

#include <memory>

#include <CxxPtr/GlibPtr.h>
#include <CxxPtr/libwebsocketsPtr.h>

#include "Http/HttpServer.h"

#include "Signalling/WsServer.h"

#include "FrontSession.h"
#include "BackSession.h"

enum {
    PING_INTERVAL = 30,
};

int SignalingServerMain(const Config& config)
{
    GMainContextPtr contextPtr(g_main_context_new());
    GMainContext* context = contextPtr.get();
    g_main_context_push_thread_default(context);

    GMainLoopPtr loopPtr(g_main_loop_new(context, FALSE));
    GMainLoop* loop = loopPtr.get();

    lws_context_creation_info wsInfo {};
    wsInfo.gid = -1;
    wsInfo.uid = -1;
#if LWS_LIBRARY_VERSION_NUMBER < 4000000
    wsInfo.ws_ping_pong_interval = PING_INTERVAL;
#else
    lws_retry_bo_t retryPolicy {};
    retryPolicy.secs_since_valid_ping = PING_INTERVAL;
    wsInfo.retry_and_idle_policy = &retryPolicy;
#endif
    wsInfo.options = LWS_SERVER_OPTION_EXPLICIT_VHOSTS;
#if defined(LWS_WITH_GLIB)
    wsInfo.options |= LWS_SERVER_OPTION_GLIB;
    wsInfo.foreign_loops = reinterpret_cast<void**>(&loop);
#endif

    LwsContextPtr lwsContextPtr(lws_create_context(&wsInfo));
    lws_context* lwsContext = lwsContextPtr.get();

    Forwarder forwarder(config);

    signalling::Config frontConfig {
        .serverName = config.serverName,
        .certificate = config.certificate,
        .key = config.key,
        .bindToLoopbackOnly = config.bindToLoopbackOnly,
        .port = config.frontPort,
        .secureBindToLoopbackOnly = false,
        .securePort = config.secureFrontPort,
    };
    signalling::WsServer frontServer(
        frontConfig, loop,
        std::bind(
            &Forwarder::createFrontSession, &forwarder,
            std::placeholders::_1, std::placeholders::_2));

    signalling::Config backConfig {
        .serverName = config.serverName,
        .certificate = config.certificate,
        .key = config.key,
        .bindToLoopbackOnly = config.bindToLoopbackOnly,
        .port = config.backPort,
        .secureBindToLoopbackOnly = false,
        .securePort = config.secureBackPort,
    };
    signalling::WsServer backServer(
        backConfig, loop,
        std::bind(
            &Forwarder::createBackSession, &forwarder,
            std::placeholders::_1, std::placeholders::_2));

    http::Server httpServer(config.httpConfig, config.frontPort, loop);

    if(httpServer.init(lwsContext) && frontServer.init(lwsContext) && backServer.init(lwsContext))
        g_main_loop_run(loop);
    else
        return -1;

    return 0;
}
