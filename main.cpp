#include <sys/stat.h>

#include <glib.h>

#include <libwebsockets.h>

#include "CxxPtr/GlibPtr.h"
#include "CxxPtr/libwebsocketsPtr.h"
#include "CxxPtr/libconfigDestroy.h"

#include "Helpers/ConfigHelpers.h"
#include "Helpers/LwsLog.h"

#include "RtspSession/Log.h"
#include "RtspSession/Session.h"
#include "Signalling/Log.h"
#include "Signalling/Config.h"
#include "Signalling/WsServer.h"
#include "Http/Log.h"
#include "Http/Config.h"
#include "Http/HttpMicroServer.h"

#include "Log.h"
#include "Config.h"
#include "stun.h"
#include "SessionsSharedData.h"
#include "Session.h"
#include "AgentSession.h"


namespace
{

const auto Log = SignalingServerLog;


bool LoadConfig(Config* config, http::Config* httpConfig)
{
    const std::deque<std::string> configDirs = ::ConfigDirs();
    if(configDirs.empty())
        return false;

    Config loadedConfig = *config;
    http::Config loadedHttpConfig = *httpConfig;

    for(const std::string& configDir: configDirs) {
        const std::string configFile = configDir + "/signaling-server.conf";
        if(!g_file_test(configFile.c_str(), G_FILE_TEST_IS_REGULAR)) {
            Log()->info("Config \"{}\" not found", configFile);
            continue;
        }

        config_t config;
        config_init(&config);
        ConfigDestroy ConfigDestroy(&config);

        Log()->info("Loading config \"{}\"", configFile);
        if(!config_read_file(&config, configFile.c_str())) {
            Log()->error("Fail load config. {}. {}:{}",
                config_error_text(&config),
                configFile,
                config_error_line(&config));
            return false;
        }

        const char* wwwRoot = nullptr;
        if(CONFIG_TRUE == config_lookup_string(&config, "www-root", &wwwRoot)) {
            loadedHttpConfig.wwwRoot = wwwRoot;
        }

        int wsPort;
        if(CONFIG_TRUE == config_lookup_int(&config, "ws-port", &wsPort))
            loadedConfig.port = static_cast<unsigned short>(wsPort);

        int httpPort = 0;
        if(CONFIG_TRUE == config_lookup_int(&config, "http-port", &httpPort))
            loadedHttpConfig.port = static_cast<unsigned short>(httpPort);

        int loopbackOnly;
        if(CONFIG_TRUE == config_lookup_bool(&config, "loopback-only", &loopbackOnly)) {
            loadedConfig.bindToLoopbackOnly = loopbackOnly != FALSE;
            loadedHttpConfig.bindToLoopbackOnly = loopbackOnly != FALSE;
        }

        const char* stunServer;
        if(CONFIG_TRUE == config_lookup_string(&config, "stun-server", &stunServer)) {
            const std::string_view stunPrefix = "stun://";
            if(0 == g_ascii_strncasecmp(stunServer, stunPrefix.data(), stunPrefix.size())) {
                loadedConfig.stunServer = stunServer;
            } else {
                Log()->error("STUN server URL should start with \"{}\"", stunPrefix);
            }
        }

        config_setting_t* debugConfig = config_lookup(&config, "debug");
        if(debugConfig && CONFIG_TRUE == config_setting_is_group(debugConfig)) {
            int logLevel = 0;
            if(CONFIG_TRUE == config_setting_lookup_int(debugConfig, "log-level", &logLevel)) {
                if(logLevel > 0) {
                    loadedConfig.logLevel =
                        static_cast<spdlog::level::level_enum>(
                            spdlog::level::critical - std::min<int>(logLevel, spdlog::level::critical));
                }
            }
            int lwsLogLevel = 0;
            if(CONFIG_TRUE == config_setting_lookup_int(debugConfig, "lws-log-level", &lwsLogLevel)) {
                if(lwsLogLevel > 0) {
                    loadedConfig.lwsLogLevel =
                        static_cast<spdlog::level::level_enum>(
                            spdlog::level::critical - std::min<int>(lwsLogLevel, spdlog::level::critical));
                }
            }
        }
    }

    bool success = true;

    if(success) {
        *config = loadedConfig;
        *httpConfig = loadedHttpConfig;
    }

    return success;
}

struct ServerSessionFactory: public WsServer::SessionFactory
{
    ServerSessionFactory(
        const Config* config,
        SessionsSharedData* sharedData
    ) : config(config), sharedData(sharedData) {}

    std::unique_ptr<rtsp::Session> createSession(
        std::optional<std::string>&& authCookie,
        const rtsp::Session::SendRequest& sendRequest,
        const rtsp::Session::SendResponse& sendResponse) noexcept override
    {
        return std::make_unique<Session>(
            sharedData,
            sendRequest,
            sendResponse);
    }

    std::unique_ptr<rtsp::Session> createAgentSession(
        std::string&& /*clientId*/,
        std::string&& agentId,
        const rtsp::Session::SendRequest& sendRequest,
        const rtsp::Session::SendResponse& sendResponse) noexcept override
    {
        return std::make_unique<AgentSession>(
            config,
            sharedData,
            std::move(agentId),
            sendRequest,
            sendResponse);
    }

private:
    const Config *const config;
    SessionsSharedData *const sharedData;
};

#ifdef SNAPCRAFT_BUILD
bool StopCoturn(bool disable)
{
    g_autoptr(GError) error = nullptr;
    gint exitStatus = 0;

    const gchar* snapName = g_getenv("SNAP_NAME");
    if(!snapName) {
        Log()->error("Can't get SNAP_NAME environment variable");

        return false;
    }

    g_autofree gchar* command =
        disable ?
            g_strdup_printf("snapctl stop %s.Coturn --disable", snapName) :
            g_strdup_printf("snapctl stop %s.Coturn", snapName);
    if(!g_spawn_command_line_sync(
        command,
        nullptr,
        nullptr,
        &exitStatus,
        &error) ||
        !g_spawn_check_wait_status(exitStatus, &error))
    {
        Log()->error(
            fmt::runtime(
                disable ?
                    "Failed to disable Coturn: {}" :
                    "Failed to stop Coturn: {}"),
            error->message);

        return false;
    }

    Log()->info(
        disable ?
            "Coturn disabled" :
            "Coturn stopped");

    return true;
}

void ConfigureCoturn(Config* config)
{
    const gchar* snapName = g_getenv("SNAP_NAME");
    if(!snapName) {
        Log()->error("Can't get SNAP_NAME environment variable");

        return;
    }

    const gchar* snapCommon = g_getenv("SNAP_COMMON");
    if(!snapCommon) {
        Log()->error("Can't get SNAP_COMMON environment variable");

        return;
    }

    g_autoptr(GError) error = nullptr;
    gint exitStatus = 0;

    g_autofree gchar* setPublicIPCommand = nullptr;
    if(config->publicIp.has_value())
        setPublicIPCommand = g_strdup_printf("snapctl set public-ip=%s", config->publicIp->c_str());
    const gchar* unsetPublicIPCommand = "snapctl unset public-ip";
    if(!g_spawn_command_line_sync(
        setPublicIPCommand ? setPublicIPCommand : unsetPublicIPCommand,
        nullptr,
        nullptr,
        &exitStatus,
        &error) ||
        !g_spawn_check_wait_status(exitStatus, &error))
    {
        Log()->error("Failed to set \"public-ip\": {}", error->message);

        return;
    }

    g_autofree gchar* pwgenStdout = nullptr;
    if(!g_spawn_command_line_sync(
        "pwgen --secure --capitalize 127",
        &pwgenStdout,
        nullptr,
        &exitStatus,
        &error) ||
        !g_spawn_check_wait_status(exitStatus, &error))
    {
        Log()->error("Failed to generate TURN REST API secret", error->message);

        return;
    }

    std::string staticAuthSecret = pwgenStdout;
    if(staticAuthSecret.back() == '\n')
        staticAuthSecret.pop_back();

    g_autofree gchar* deleteSecretsCmd = g_strdup_printf(
        "turnadmin --db=%s/turndb --delete-all-secret --realm=%s",
        snapCommon,
        snapName);

    if(!g_spawn_command_line_sync(
        deleteSecretsCmd,
        nullptr,
        nullptr,
        &exitStatus,
        &error) ||
        !g_spawn_check_wait_status(exitStatus, &error))
    {
        Log()->error("Failed to delete old TURN REST API secrets: {}", error->message);

        return;
    }

    g_autofree gchar* setSecretCmd = g_strdup_printf(
        "turnadmin --db=%s/turndb --set-secret=%s --realm=%s",
        snapCommon,
        staticAuthSecret.c_str(),
        snapName);

    if(!g_spawn_command_line_sync(
        setSecretCmd,
        nullptr,
        nullptr,
        &exitStatus,
        &error) ||
        !g_spawn_check_wait_status(exitStatus, &error))
    {
        Log()->error("Failed to set TURN REST API secret: {}", error->message);

        return;
    }

    g_autofree gchar* startCommand =
        g_strdup_printf("snapctl start %s.Coturn", snapName);
    if(!g_spawn_command_line_sync(
        startCommand,
        nullptr,
        nullptr,
        &exitStatus,
        &error) ||
        !g_spawn_check_wait_status(exitStatus, &error))
    {
        Log()->error("Failed to enable Coturn: {}", error->message);

        return;
    }

    Log()->info("Coturn configured and started");

    config->turnStaticAuthSecret = std::move(staticAuthSecret);
}
#endif

}

int main(int argc, char *argv[])
{
    g_set_prgname("org.webrtsp.signaling-server");

    umask(S_IWGRP | S_IRWXO); // rwxr-x---

    {
        g_autofree gchar* clientId = nullptr;
        GOptionEntry optionEntries[] = {
            {
                "generate-credentials",
                'g',
                0,
                G_OPTION_ARG_STRING,
                &clientId,
                "Generate new Agent ID and Access Token pair for the specified Client ID",
                "<CLIENT_ID>"
            },
            { nullptr }
        };

        g_autoptr(GOptionContext) optionContext = g_option_context_new(nullptr);
        g_option_context_add_main_entries(optionContext, optionEntries, nullptr);
        g_autoptr(GError) optionError = nullptr;
        if(!g_option_context_parse(optionContext, &argc, &argv, &optionError)) {
            Log()->error("Failed to parse command line options: {}", optionError->message);
            return -1;
        }

        if(clientId) {
            AgentsDb agentsDb;
            if(!agentsDb.isOpen()) {
                Log()->error("Failed to open Agents DB");
                return -1;
            }

            std::optional<AgentsDb::AgentCredentials> credentials =
                agentsDb.registerAgent(clientId);
            if(!credentials.has_value()) {
                Log()->error("Failed to generate Agent credentials");
                return -1;
            }

            printf(
                "Agent ID: %s\n"
                "Access token: %s\n",
                credentials->agentId.c_str(),
                credentials->accessToken.c_str());

            return 0;
        }
    }

    Config config {};
    http::Config httpConfig {};
    httpConfig.indexPaths.try_emplace("/", false);
    httpConfig.indexPaths.try_emplace("/view", false);
#ifndef NDEBUG
    config.bindToLoopbackOnly = false;
    httpConfig.bindToLoopbackOnly = false;
#endif

#ifdef SNAPCRAFT_BUILD
    const gchar* snapPath = g_getenv("SNAP");
    const gchar* snapName = g_getenv("SNAP_NAME");
    if(snapPath && snapName) {
        GCharPtr wwwRootPtr(g_build_path(G_DIR_SEPARATOR_S, snapPath, "opt", snapName, "www", NULL));
        httpConfig.wwwRoot = wwwRootPtr.get();
    }
#endif

    if(!LoadConfig(&config, &httpConfig))
        return -1;

#ifdef SNAPCRAFT_BUILD
    const gchar* snapCommon = g_getenv("SNAP_COMMON");
    if(!g_path_is_absolute(httpConfig.wwwRoot.c_str()) && snapCommon) {
        GCharPtr wwwRootPtr(g_build_path(G_DIR_SEPARATOR_S, snapCommon, httpConfig.wwwRoot.c_str(), NULL));
        httpConfig.wwwRoot = wwwRootPtr.get();
    }
#endif

#ifdef SNAPCRAFT_BUILD
    const bool disableCoturn = false;
    // to workaround "error running snapctl: snap "rtsp-to-webrtsp" has "install-snap" change in progress"
    // have to try multiple times
    for(guint i = 0; i <= 3; ++i) {
        if(i != 0) {
            const int delay = i;
            Log()->info("Sleeping for {} seconds before try to disable Coturn another time...", delay);
            sleep(delay);
        }
        if(StopCoturn(disableCoturn))
            break;
    }
#endif

    config.publicIp = DetectPublicIP(config.stunServer);

#if defined(SNAPCRAFT_BUILD) && !defined(BUILD_AS_CAMERA_STREAMER) && !defined(BUILD_AS_V4L2_RESTREAMER)
    if(!disableCoturn)
        ConfigureCoturn(&config);
#endif

    InitLwsLogger(config.lwsLogLevel);
    InitWsServerLogger(config.logLevel);
    rtsp::InitSessionLogger(config.logLevel);
    InitSignalingServerLogger(config.logLevel);
    InitHttpServerLogger(config.logLevel);

    GMainContextPtr contextPtr(g_main_context_new());
    GMainContext* context = contextPtr.get();
    g_main_context_push_thread_default(context);

    GMainLoopPtr loopPtr(g_main_loop_new(context, FALSE));
    GMainLoop* loop = loopPtr.get();
    lws_context_creation_info lwsInfo {};
    lwsInfo.gid = -1;
    lwsInfo.uid = -1;
    lwsInfo.options = LWS_SERVER_OPTION_EXPLICIT_VHOSTS;
#if defined(LWS_WITH_GLIB)
    lwsInfo.options |= LWS_SERVER_OPTION_GLIB;
    lwsInfo.foreign_loops = reinterpret_cast<void**>(&loop);
#endif

    LwsContextPtr lwsContextPtr(lws_create_context(&lwsInfo));
    lws_context* lwsContext = lwsContextPtr.get();

    SessionsSharedData sessionsSharedData {};

    ServerSessionFactory serverSessionFactory(&config, &sessionsSharedData);

    std::unique_ptr<WsServer> wsServerPtr = std::make_unique<WsServer>(
        config,
        &serverSessionFactory,
        &sessionsSharedData.agentsDb);

    std::unique_ptr<http::MicroServer> httpServerPtr;
    if(httpConfig.port) {
        std::string configJs = fmt::format(
            "const WebRTSPPort = {};\r\n",
            config.port);
        if(0 == config.stunServer.compare(0, 7, "stun://")) {
            std::string iceServer = config.stunServer;
            iceServer.erase(5, 2); // "stun://..." -> "stun:..."
            configJs += fmt::format("const STUNServer = \"{}\";\r\n", iceServer);
        }

        httpServerPtr =
            std::make_unique<http::MicroServer>(
                httpConfig,
                configJs,
                http::MicroServer::OnNewAuthToken(),
                context);
    }

    if(
        (!wsServerPtr || wsServerPtr->init(loop, lwsContext)) &&
        (!httpServerPtr || httpServerPtr->init())
    ) {
        g_main_loop_run(loop);
    } else
        return -1;

    return 0;
}
