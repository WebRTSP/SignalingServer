#include "AgentsDb.h"

#include <cmath>
#include <cassert>
#include <string_view>

#include <sys/random.h>
#include <sys/stat.h>

#include <glib.h>
#include <sqlite3.h>

#include <Helpers/ConfigHelpers.h>


namespace
{

const char *const DbName = "agents.db";

enum {
    ZERO_TERMINATED = -1,
};

enum {
    EMPTY_DB = 0,
    DB_VERSION_1 = 1,
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC(sqlite3_stmt, sqlite3_finalize)

enum {
    AGENT_ID_BASE_TIME = 1735664400,
    AGENT_ID_RAW_SIZE = 80 / 8,
    AGENT_ID_TIME_PREFIX_SIZE = 48 / 8,
    AGENT_ID_RANDOM_SUFFIX_SIZE = AGENT_ID_RAW_SIZE - AGENT_ID_TIME_PREFIX_SIZE,
    AGENT_ID_SIZE =
        static_cast<unsigned>(AGENT_ID_TIME_PREFIX_SIZE / 5.f * 8 + .9f) +
        static_cast<unsigned>(AGENT_ID_RANDOM_SUFFIX_SIZE / 5.f * 8 + .9f),

    AUTH_TOKEN_RAW_SIZE = 32,
};

typedef std::string ClientId;
typedef std::string AgentId;
typedef std::string AuthToken;

enum: std::string::value_type {
    AGENT_ID_LTRIM_CHAR = '0'
};

const char CrockfordBase32Alphabet[] = "0123456789abcdefghjkmnpqrstvwxyz";

std::string Base32Encode(const uint8_t* data, unsigned size)
{
    const unsigned resultSize = ceil(size / 5.f * 8);
    std::string result;
    result.reserve(resultSize);

    uint8_t high = 0;
    for(unsigned i = 0; i < resultSize; ++i) {
        const unsigned sourceOffset = (i + 1) * 5 / 8;
        const uint8_t in = sourceOffset < size ? data[sourceOffset] : 0;
        unsigned outOffset = 0;
        switch(i % 8) {
        case 0:
            outOffset = in >> 3;
            high = (in & 0x7) << 2;
            break;
        case 1:
            outOffset = high | ((in & 0xc0) >> 6);
            high = (in & 0x3e) >> 1;
            break;
        case 2:
            outOffset = high;
            high = (in & 0x1) << 4;
            break;
        case 3:
            outOffset = high | ((in & 0xf0) >> 4);
            high = (in & 0xf) << 1;
            break;
        case 4:
            outOffset = high | ((in & 0x80) >> 7);
            high = (in & 0x7c) >> 2;
            break;
        case 5:
            outOffset = high;
            high = (in & 0x3) << 3;
            break;
        case 6:
            outOffset = high | ((in & 0xe0) >> 5);
            high = (in & 0x1f);
            break;
        case 7:
            outOffset = high;
            break;
        }

        result.push_back(CrockfordBase32Alphabet[outOffset]);
    }

    return result;
}

std::optional<AgentId> GenAgentId() noexcept
{
    uint8_t random[AGENT_ID_RANDOM_SUFFIX_SIZE];
    if(getrandom(random, sizeof(random), 0) == -1)
        return {};

    const guint64 now =
        GUINT64_TO_BE(static_cast<guint64>(g_get_real_time()) / 1000 - AGENT_ID_BASE_TIME * 1000ull);
    auto now_array = reinterpret_cast<const uint8_t*>(&now);

    return
        Base32Encode(
            reinterpret_cast<const uint8_t*>(&now) + sizeof(now) - AGENT_ID_TIME_PREFIX_SIZE,
            AGENT_ID_TIME_PREFIX_SIZE) +
        Base32Encode(random, sizeof(random));
}

std::optional<AuthToken> GenAuthToken() noexcept
{
    uint8_t authToken[AUTH_TOKEN_RAW_SIZE];
    if(getrandom(authToken, sizeof(authToken), 0) == -1)
        return {};

    g_autofree gchar* base64AuthToken = g_base64_encode(authToken, sizeof(authToken));
    std::string token = base64AuthToken;
    for(std::string::value_type& c: token) {
        if(c == '+')
            c = '-';
        else if(c == '/')
            c = '_';
        else if(c == '=')
            break;
    }
    const std::string::size_type truncatePos = token.find_last_not_of('=');
    if(truncatePos == std::string::npos)
        return {}; // not possible actually

    token.resize(truncatePos + 1);

    return token;
}

std::string ShortenAgentId(const AgentId& agentId)
{
    std::string prefixedAgentId = std::string(1, AgentsDb::AGENT_ID_PREFIX);
    prefixedAgentId += std::string_view(agentId).substr(agentId.find_first_not_of(AGENT_ID_LTRIM_CHAR));
    return prefixedAgentId;
}

std::string RestoreAgentId(const AgentId& prefixedAgentId)
{
    assert(!prefixedAgentId.empty() && prefixedAgentId.front() == AgentsDb::AGENT_ID_PREFIX);

    std::string agentId =
        std::string(AGENT_ID_SIZE - (prefixedAgentId.size() - 1), AGENT_ID_LTRIM_CHAR);
    agentId += std::string_view(prefixedAgentId).substr(1);
    return agentId;
}

}


struct AgentsDb::Private
{
    bool open() noexcept;
    void close() noexcept;

    std::optional<int> dbVersion() const noexcept;
    bool initSchema() noexcept;
    bool prepareStatements() noexcept;
    void finalizeStatements() noexcept;
    bool insertAgent(
        const ClientId& clientId,
        const AgentId& agentId,
        const AuthToken& authToken) noexcept;
    bool checkAgentExists(const AgentId& agentId) noexcept;
    bool checkAgentToken(
        const ClientId& clientId,
        const AgentId& agentId,
        const AuthToken&) noexcept;

    std::optional<AgentsDb::AgentCredentials> registerAgent(const std::string& clientId) noexcept;

    sqlite3* db = nullptr;
    sqlite3_stmt* insertAgentStmt = nullptr;
    sqlite3_stmt* checkAgentStmt = nullptr;
    sqlite3_stmt* checkAgentTokenStmt = nullptr;
};

std::optional<int> AgentsDb::Private::dbVersion() const noexcept
{
    if(!db)
        return {};

    g_autoptr(sqlite3_stmt) stmt = nullptr;

    if(
        sqlite3_prepare_v2(db, "PRAGMA user_version;", ZERO_TERMINATED, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW
    ) {
        return sqlite3_column_int(stmt, 0);
    }

    return {};
}

bool AgentsDb::Private::initSchema() noexcept
{
    const char *const sql =
        "CREATE TABLE IF NOT EXISTS agents ( "
        "    agent_id TEXT NOT NULL PRIMARY KEY, "
        "    client_id TEXT NOT NULL, "
        "    token TEXT NOT NULL, "
        "    created_at INTEGER NOT NULL DEFAULT (unixepoch()) "
        ") WITHOUT ROWID; "
        "PRAGMA user_version = 1;" ;
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool AgentsDb::Private::prepareStatements() noexcept
{
    assert(!insertAgentStmt);
    assert(!checkAgentStmt);
    assert(!checkAgentTokenStmt);

    const char *const insertSql =
        "INSERT INTO agents(agent_id, client_id, token) "
        "VALUES(?, ?, ?)";

    if(sqlite3_prepare_v2(db, insertSql, ZERO_TERMINATED, &insertAgentStmt, nullptr) != SQLITE_OK)
        return false;

    const char *const checkSql =
        "SELECT 1 FROM agents where agent_id = ?";

    if(sqlite3_prepare_v2(db, checkSql, ZERO_TERMINATED, &checkAgentStmt, nullptr) != SQLITE_OK)
        return false;

    const char *const checkTokenSql =
        "SELECT 1 FROM agents where agent_id = ? and client_id = ? and token = ?";

    if(sqlite3_prepare_v2(db, checkTokenSql, ZERO_TERMINATED, &checkAgentTokenStmt, nullptr) != SQLITE_OK)
        return false;

    return true;
}

void AgentsDb::Private::finalizeStatements() noexcept
{
    if(checkAgentTokenStmt) {
        sqlite3_finalize(checkAgentTokenStmt);
        checkAgentTokenStmt = nullptr;
    }

    if(checkAgentStmt) {
        sqlite3_finalize(checkAgentStmt);
        checkAgentStmt = nullptr;
    }

    if(insertAgentStmt) {
        sqlite3_finalize(insertAgentStmt);
        insertAgentStmt = nullptr;
    }
}

bool AgentsDb::Private::insertAgent(
    const AgentId& clientId,
    const std::string& agentId,
    const AuthToken& authToken) noexcept
{
    if(!insertAgentStmt)
        return false;

    const bool success =
        sqlite3_bind_text(insertAgentStmt, 1, agentId.c_str(), ZERO_TERMINATED, SQLITE_STATIC) == SQLITE_OK &&
        sqlite3_bind_text(insertAgentStmt, 2, clientId.c_str(), ZERO_TERMINATED, SQLITE_STATIC) == SQLITE_OK &&
        sqlite3_bind_text(insertAgentStmt, 3, authToken.c_str(), ZERO_TERMINATED, SQLITE_STATIC) == SQLITE_OK &&
        sqlite3_step(insertAgentStmt) == SQLITE_DONE;

    sqlite3_reset(insertAgentStmt);

    return success;
}

bool AgentsDb::Private::checkAgentExists(const AgentId& agentId) noexcept
{
    assert(agentId.size() == AGENT_ID_SIZE);

    if(!checkAgentStmt)
        return false;

    const bool success =
        sqlite3_bind_text(checkAgentStmt, 1, agentId.c_str(), ZERO_TERMINATED, SQLITE_STATIC) == SQLITE_OK &&
        sqlite3_step(checkAgentStmt) == SQLITE_ROW;

    sqlite3_reset(checkAgentStmt);

    return success;
}

bool AgentsDb::Private::checkAgentToken(
    const ClientId& clientId,
    const AgentId& agentId,
    const AuthToken& authToken) noexcept
{
    assert(agentId.size() == AGENT_ID_SIZE);

    if(!checkAgentTokenStmt)
        return false;

    const bool success =
        sqlite3_bind_text(checkAgentTokenStmt, 1, agentId.c_str(), ZERO_TERMINATED, SQLITE_STATIC) == SQLITE_OK &&
        sqlite3_bind_text(checkAgentTokenStmt, 2, clientId.c_str(), ZERO_TERMINATED, SQLITE_STATIC) == SQLITE_OK &&
        sqlite3_bind_text(checkAgentTokenStmt, 3, authToken.c_str(), ZERO_TERMINATED, SQLITE_STATIC) == SQLITE_OK &&
        sqlite3_step(checkAgentTokenStmt) == SQLITE_ROW;

    sqlite3_reset(checkAgentTokenStmt);

    return success;
}

bool AgentsDb::Private::open() noexcept
{
    if(db)
        return false;

    const std::optional<std::string> dataDir = DataDir();
    if(!dataDir.has_value())
        return false;

    if(g_mkdir_with_parents(dataDir.value().c_str(), S_IRWXU | S_IRWXG) < 0)
        return false;

    const std::string dbPath = FullPath(dataDir.value(), DbName);

    if(sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK)
        return false;

    sqlite3_busy_timeout(db, 10 * 1000);

    if(sqlite3_exec(db, "BEGIN EXCLUSIVE;", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;

    const std::optional<int> v = dbVersion();
    if(!v.has_value())
        return false;

    switch(v.value()) {
    case EMPTY_DB:
        if(!initSchema())
            return false;
        break;
    case DB_VERSION_1:
        break;
    }

    if(sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;

    return prepareStatements();
}

void AgentsDb::Private::close() noexcept
{
    finalizeStatements();

    if(db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

std::optional<AgentsDb::AgentCredentials>
AgentsDb::Private::registerAgent(const std::string& clientId) noexcept
{
    for(unsigned i = 0; i < 5; ++i) {
        std::optional<AgentId> agentId = GenAgentId();
        if(!agentId.has_value())
            continue;

        assert(agentId->size() == AGENT_ID_SIZE);

        std::optional<AuthToken> token = GenAuthToken();
        if(!token.has_value())
            continue;

        if(!insertAgent(clientId, agentId.value(), token.value()))
            continue;

        return AgentCredentials {
            std::move(ShortenAgentId(*agentId)),
            std::move(token.value()) };
    }

    return {};
}

AgentsDb::AgentsDb() noexcept :
    _p(std::make_unique<Private>())
{
    if(!_p->open())
        _p->close();
}

AgentsDb::~AgentsDb() noexcept
{
    _p->close();
}

bool AgentsDb::isOpen() const noexcept
{
    return _p->db != nullptr;
}

std::optional<AgentsDb::AgentCredentials>
AgentsDb::registerAgent(const std::string& clientId) noexcept
{
    return _p->registerAgent(clientId);
}

bool AgentsDb::authenticateAgent(
    const std::string& clientId,
    const std::string& agentId,
    const std::string& accessToken) noexcept
{
    return _p->checkAgentToken(clientId, RestoreAgentId(agentId), accessToken);
}

bool AgentsDb::isRegistered(const AgentId& agentId) const noexcept
{
    return _p->checkAgentExists(RestoreAgentId(agentId));
}
